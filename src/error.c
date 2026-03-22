#include "error.h"

#include <stdio.h>
#include <stdlib.h>

#include "poison.h"

void *talloc_zero_for_error(TALLOC_CTX *ctx, size_t size)
{
    return talloc_zero_size(ctx, size);
}

__attribute__((noreturn)) void fx_panic_impl(const char *msg, const char *file, int line)
{
    (void)fprintf(stderr, "PANIC %s [%s:%d]\n", msg, file, line);
    abort();
}
