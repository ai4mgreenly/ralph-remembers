#include "config/config.h"
#include "log/log.h"

#include <signal.h>
#include <stdio.h>
#include <talloc.h>
#include <unistd.h>

volatile sig_atomic_t g_shutdown = 0;

void handle_signal(int signum);

void handle_signal(int signum)
{
    (void)signum;
    g_shutdown = 1;
}

int main(int argc, char **argv)
{
    TALLOC_CTX *ctx = talloc_new(NULL);

    res_t res = fx_cfg_load(ctx, argc, (const char **)(void *)argv);
    if (is_err(&res)) {
        fprintf(stderr, "fandex: %s\n", res.err->msg);
        talloc_free(ctx);
        return 1;
    }

    fx_cfg_t *cfg = res.ok;

    if (cfg->help) {
        printf("usage: fandex [--watch PATH] [--db PATH] [--socket PATH]\n"
               "              [--log-level LEVEL] [-h]\n"
               "\n"
               "  --watch PATH        directory to watch (default: %s)\n"
               "  --db PATH           database path (default: %s)\n"
               "  --socket PATH       socket path (default: %s)\n"
               "  --log-level LEVEL   log level: debug, info, warn, error (default: info)\n"
               "  -h, --help          show this help\n",
               cfg->watch_path, cfg->db_path, cfg->socket_path);
        talloc_free(ctx);
        return 0;
    }

    fx_log_t *log = fx_log_init(ctx, stderr, cfg->log_level);

    const char *level_names[] = {"debug", "info", "warn", "error"};

    fx_log_info(log, "fandex starting");
    fx_log_info(log, "watch_path=%s", cfg->watch_path);
    fx_log_info(log, "db_path=%s", cfg->db_path);
    fx_log_info(log, "socket_path=%s", cfg->socket_path);
    fx_log_info(log, "log_level=%s", level_names[cfg->log_level]);

    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fx_log_info(log, "entering main loop");
    while (!g_shutdown) {
        usleep(100000);
    }
    fx_log_info(log, "received shutdown signal");

    fx_log_info(log, "fandex stopping");

    talloc_free(ctx);
    return 0;
}
