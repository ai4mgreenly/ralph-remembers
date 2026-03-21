#ifndef FX_CONFIG_ENV_H
#define FX_CONFIG_ENV_H

#include "config/config.h"

// Populate cfg with compiled-in defaults, then apply env var overrides.
// OOM calls PANIC(). Returns OK(NULL) on success, ERR() on failure.
res_t fx_cfg_env_load(fx_cfg_t *cfg);

#endif
