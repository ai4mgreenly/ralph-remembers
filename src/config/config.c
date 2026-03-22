#include "config/config.h"
#include "config/config_env.h"
#include "config/config_args.h"
#include <talloc.h>

#include "poison.h"

res_t fx_cfg_load(TALLOC_CTX *ctx, int argc, const char **argv)
{
    fx_cfg_t *cfg = talloc_zero(ctx, fx_cfg_t);
    if (!cfg) {
        PANIC("Out of memory");
    }

    res_t r = fx_cfg_env_load(cfg);
    if (is_err(&r)) {
        talloc_free(cfg);
        return r;
    }

    r = fx_cfg_args_apply(cfg, argc, argv);
    if (is_err(&r)) {
        talloc_free(cfg);
        return r;
    }

    return OK(cfg);
}

void fx_cfg_free(fx_cfg_t *cfg)
{
    talloc_free(cfg);
}
