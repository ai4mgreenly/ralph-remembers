#ifndef FX_CONFIG_H
#define FX_CONFIG_H

#include <stdbool.h>
#include <talloc.h>

#include "result.h"

typedef struct fx_cfg {
    char *watch_path;
    char *db_path;
    char *socket_path;
    bool  help;
} fx_cfg_t;

// Load config: env vars override compiled-in defaults; CLI args override env.
//   FANDEX_WATCH_PATH  (default: ~/projects)
//   FANDEX_DB_PATH     (default: ~/.local/state/fandex/fandex.db)
//   FANDEX_SOCKET_PATH (default: /run/user/<uid>/fandex/fandex.sock)
//
// Allocates cfg as a child of ctx. On success returns OK(cfg).
// On unknown flag or missing value returns ERR(). OOM calls PANIC().
// Returns non-NULL cfg with cfg->help set if --help/-h was passed.
// Caller may call talloc_free(cfg) or let parent context handle cleanup.
res_t fx_cfg_load(TALLOC_CTX *ctx, int argc, const char **argv);

void fx_cfg_free(fx_cfg_t *cfg);

#endif
