#include "config/config_env.h"
#include "config/defaults.h"
#include <talloc.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include "poison.h"

res_t fx_cfg_env_load(fx_cfg_t *cfg)
{
    const char *home = getenv("HOME");
    if (!home) {
        home = "/tmp";
    }

    const char *watch_env = getenv(FX_ENV_WATCH_PATH);
    if (watch_env) {
        cfg->watch_path = talloc_strdup(cfg, watch_env);
    } else {
        cfg->watch_path = talloc_asprintf(cfg, "%s%s", home, FX_DEFAULT_WATCH_PATH_SUFFIX);
    }
    if (!cfg->watch_path) {
        PANIC("Out of memory");
    }

    const char *db_env = getenv(FX_ENV_DB_PATH);
    if (db_env) {
        cfg->db_path = talloc_strdup(cfg, db_env);
    } else {
        cfg->db_path = talloc_asprintf(cfg, "%s%s", home, FX_DEFAULT_DB_PATH_SUFFIX);
    }
    if (!cfg->db_path) {
        PANIC("Out of memory");
    }

    const char *sock_env = getenv(FX_ENV_SOCKET_PATH);
    if (sock_env) {
        cfg->socket_path = talloc_strdup(cfg, sock_env);
    } else {
        const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
        if (xdg_runtime) {
            cfg->socket_path = talloc_asprintf(cfg, "%s%s", xdg_runtime,
                                               FX_DEFAULT_SOCKET_PATH_SUFFIX);
        } else {
            uid_t uid = getuid();
            cfg->socket_path = talloc_asprintf(cfg, "/run/user/%u%s", (unsigned)uid,
                                               FX_DEFAULT_SOCKET_PATH_SUFFIX);
        }
    }
    if (!cfg->socket_path) {
        PANIC("Out of memory");
    }

    return OK(NULL);
}
