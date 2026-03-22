#ifndef FX_WRAPPER_POSIX_H
#define FX_WRAPPER_POSIX_H

#include "wrapper_base.h"

#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sys/fanotify.h>
#include <unistd.h>

#ifdef NDEBUG

// Release: static inline - real syscall inlined, zero overhead
static inline int32_t posix_fanotify_init_(uint32_t flags, uint32_t event_f_flags)
{
    return (int32_t)fanotify_init(flags, event_f_flags);
}

static inline int32_t posix_fanotify_mark_(int32_t fanotify_fd,
                                           uint32_t flags,
                                           uint64_t mask,
                                           int32_t dirfd,
                                           const char *pathname)
{
    return (int32_t)fanotify_mark((int)fanotify_fd, flags, mask, (int)dirfd, pathname);
}

static inline int32_t posix_open_(const char *pathname, int32_t flags)
{
    return (int32_t)open(pathname, (int)flags);
}

static inline int32_t posix_close_(int32_t fd)
{
    return (int32_t)close((int)fd);
}

static inline int32_t posix_poll_(struct pollfd *fds, nfds_t nfds, int32_t timeout)
{
    return (int32_t)poll(fds, nfds, (int)timeout);
}

#else

// Debug: weak symbol declarations (defined in wrapper_posix.c, overridable in tests)
int32_t posix_fanotify_init_(uint32_t flags, uint32_t event_f_flags);
int32_t posix_fanotify_mark_(int32_t fanotify_fd, uint32_t flags, uint64_t mask, int32_t dirfd, const char *pathname);
int32_t posix_open_(const char *pathname, int32_t flags);
int32_t posix_close_(int32_t fd);
int32_t posix_poll_(struct pollfd *fds, nfds_t nfds, int32_t timeout);

#endif // NDEBUG

#endif // FX_WRAPPER_POSIX_H
