#include "watch/watch.h"

#include "error.h"
#include "log/log.h"
#include "wrapper_posix.h"

#include "poison.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/fanotify.h>
#include <talloc.h>
#include <unistd.h>

extern volatile sig_atomic_t g_shutdown;

struct fx_watch {
    int32_t fan_fd;
    fx_log_t *log;
    const char *watch_path;
};

const char *fx_watch_event_name(uint32_t mask)
{
    if (mask & FAN_CREATE) {
        return "create";
    }
    if (mask & FAN_DELETE) {
        return "delete";
    }
    if (mask & FAN_MODIFY) {
        return "modify";
    }
    if (mask & FAN_MOVED_FROM) {
        return "moved_from";
    }
    if (mask & FAN_MOVED_TO) {
        return "moved_to";
    }
    return NULL;
}

bool fx_watch_path_under(const char *watch_path, const char *path)
{
    if (!watch_path || !path) {
        return false;
    }

    size_t wlen = strlen(watch_path);

    // Strip trailing slashes from effective length (keep at least 1 char)
    while (wlen > 1 && watch_path[wlen - 1] == '/') {
        wlen--;
    }

    if (strncmp(path, watch_path, wlen) != 0) {
        return false;
    }

    // Exact match or next char must be '/' to avoid prefix false positives
    return path[wlen] == '\0' || path[wlen] == '/';
}

res_t fx_watch_init(TALLOC_CTX *ctx, fx_log_t *log, const char *watch_path)
{
    int32_t fan_fd = posix_fanotify_init_(
        FAN_CLASS_NOTIF | FAN_REPORT_FID | FAN_REPORT_DFID_NAME,
        0);
    if (fan_fd < 0) {
        return ERR(ctx, IO, "fanotify_init failed: %s", strerror(errno));
    }

    uint64_t mask = FAN_CREATE | FAN_DELETE | FAN_MODIFY | FAN_MOVED_FROM | FAN_MOVED_TO;
    int32_t rc = posix_fanotify_mark_(
        fan_fd,
        FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
        mask,
        AT_FDCWD,
        watch_path);
    if (rc < 0) {
        posix_close_(fan_fd);
        return ERR(ctx, IO, "fanotify_mark failed: %s", strerror(errno));
    }

    fx_watch_t *w = talloc_zero(ctx, fx_watch_t);
    if (!w) PANIC("Out of memory"); // LCOV_EXCL_BR_LINE

    w->fan_fd = fan_fd;
    w->log = log;
    w->watch_path = watch_path;

    fx_log_info(log, "watch init watch_path=%s", watch_path);
    return OK(w);
}

// Forward declaration - processes one fanotify event
void process_fan_event(fx_watch_t *w, struct fanotify_event_metadata *meta);

void process_fan_event(fx_watch_t *w, struct fanotify_event_metadata *meta)
{
    struct fanotify_event_info_fid *fid =
        (struct fanotify_event_info_fid *)(void *)(meta + 1);

    if (fid->hdr.info_type != FAN_EVENT_INFO_TYPE_DFID_NAME) {
        return;
    }

    struct file_handle *fh = (struct file_handle *)(void *)fid->handle;
    const char *fname = (const char *)(const void *)(fh->f_handle + fh->handle_bytes);

    int dir_fd = open_by_handle_at(AT_FDCWD, fh,
                                   O_RDONLY | O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (dir_fd < 0) {
        return;
    }

    char proc_path[64];
    char dir_path[PATH_MAX];
    (void)snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", dir_fd);
    ssize_t n = readlink(proc_path, dir_path, sizeof(dir_path) - 1);
    close(dir_fd);

    if (n <= 0) {
        return;
    }

    dir_path[n] = '\0';
    char *full_path = talloc_asprintf(NULL, "%s/%s", dir_path, fname);
    if (!full_path) PANIC("Out of memory"); // LCOV_EXCL_BR_LINE

    if (fx_watch_path_under(w->watch_path, full_path)) {
        const char *evname = fx_watch_event_name((uint32_t)meta->mask);
        if (evname) {
            fx_log_info(w->log, "event type=%s path=%s", evname, full_path);
        }
    }
    talloc_free(full_path);
}

void fx_watch_run(fx_watch_t *w)
{
    char buf[4096];
    struct pollfd pfd = {.fd = (int)w->fan_fd, .events = POLLIN};

    while (!g_shutdown) {
        int32_t ret = posix_poll_(&pfd, 1, 200);
        if (ret < 0 && errno != EINTR) {
            break;
        }
        if (ret <= 0) {
            continue;
        }

        ssize_t len = read((int)w->fan_fd, buf, sizeof(buf));
        if (len <= 0) {
            continue;
        }

        struct fanotify_event_metadata *meta =
            (struct fanotify_event_metadata *)(void *)buf;
        while (FAN_EVENT_OK(meta, (size_t)len)) {
            if (meta->vers != FANOTIFY_METADATA_VERSION) {
                break;
            }
            process_fan_event(w, meta);
            meta = FAN_EVENT_NEXT(meta, len);
        }
    }
}

void fx_watch_free(fx_watch_t *w)
{
    fx_log_info(w->log, "watch free");
    posix_close_(w->fan_fd);
    talloc_free(w);
}
