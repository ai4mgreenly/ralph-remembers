#ifndef FX_RESULT_H
#define FX_RESULT_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <talloc.h>

typedef enum {
    ERR_NONE                = 0,
    ERR_INVALID_ARG         = 1,
    ERR_OUT_OF_RANGE        = 2,
    ERR_IO                  = 3,
    ERR_PARSE               = 4,
    ERR_DB_CONNECT          = 5,
    ERR_DB_MIGRATE          = 6,
    ERR_OUT_OF_MEMORY       = 7,
    ERR_AGENT_NOT_FOUND     = 8,
    ERR_PROVIDER            = 9,
    ERR_MISSING_CREDENTIALS = 10,
    ERR_NOT_IMPLEMENTED     = 11,
} err_code_t;

typedef struct err {
    err_code_t  code;
    const char *file;
    int32_t     line;
    char        msg[256];
} err_t;

typedef struct {
    union { void *ok; err_t *err; };
    bool is_err;
} res_t;

#define PANIC(fmt, ...) \
    do { \
        (void)fprintf(stderr, "PANIC " fmt "\n", ##__VA_ARGS__); \
        abort(); \
    } while (0)

#define OK(value) ((res_t){ .ok = (void *)(value), .is_err = false })

#define ERR(ctx, code_, fmt, ...) \
    __extension__({ \
        err_t *_err = talloc_zero((ctx), err_t); \
        if (!_err) PANIC("Out of memory"); \
        _err->code = (code_); \
        _err->file = __FILE__; \
        _err->line = (int32_t)(__LINE__); \
        (void)snprintf(_err->msg, sizeof(_err->msg), "" fmt, ##__VA_ARGS__); \
        (res_t){ .err = _err, .is_err = true }; \
    })

#define is_ok(r)  (!(r)->is_err)
#define is_err(r) ((r)->is_err)

#endif // FX_RESULT_H
