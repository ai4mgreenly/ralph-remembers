#ifndef FX_CONFIG_ARGS_H
#define FX_CONFIG_ARGS_H

#include "config/config.h"

// Apply CLI argument overrides onto cfg.
// Recognises --watch, --db, --socket, --help / -h.
// Returns OK(NULL) on success, ERR() on unknown flag or missing value.
// OOM calls PANIC().
res_t fx_cfg_args_apply(fx_cfg_t *cfg, int argc, const char **argv);

#endif
