#include "config/config_env.h"
#include "config/config.h"
#include "config/defaults.h"
#include "result.h"

#include <talloc.h>

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

static void clear_env(void)
{
    unsetenv("FANDEX_WATCH_PATH");
    unsetenv("FANDEX_DB_PATH");
    unsetenv("FANDEX_SOCKET_PATH");
}

START_TEST(test_env_no_vars) {
    clear_env();

    TALLOC_CTX *ctx = talloc_new(NULL);
    fx_cfg_t *cfg = talloc_zero(ctx, fx_cfg_t);
    res_t r = fx_cfg_env_load(cfg);
    ck_assert(!r.is_err);

    const char *home = getenv("HOME");
    uid_t uid = getuid();
    char expected[512];

    (void)snprintf(expected, sizeof(expected), "%s%s", home, FX_DEFAULT_WATCH_PATH_SUFFIX);
    ck_assert_str_eq(cfg->watch_path, expected);

    (void)snprintf(expected, sizeof(expected), "%s%s", home, FX_DEFAULT_DB_PATH_SUFFIX);
    ck_assert_str_eq(cfg->db_path, expected);

    (void)snprintf(expected, sizeof(expected), "/run/user/%u%s", (unsigned)uid,
                   FX_DEFAULT_SOCKET_PATH_SUFFIX);
    ck_assert_str_eq(cfg->socket_path, expected);

    talloc_free(ctx);
}
END_TEST

START_TEST(test_env_watch_path) {
    clear_env();
    setenv("FANDEX_WATCH_PATH", "/tmp/watch", 1);

    TALLOC_CTX *ctx = talloc_new(NULL);
    fx_cfg_t *cfg = talloc_zero(ctx, fx_cfg_t);
    res_t r = fx_cfg_env_load(cfg);
    ck_assert(!r.is_err);

    ck_assert_str_eq(cfg->watch_path, "/tmp/watch");

    const char *home = getenv("HOME");
    char expected[512];

    (void)snprintf(expected, sizeof(expected), "%s%s", home, FX_DEFAULT_DB_PATH_SUFFIX);
    ck_assert_str_eq(cfg->db_path, expected);

    uid_t uid = getuid();
    (void)snprintf(expected, sizeof(expected), "/run/user/%u%s", (unsigned)uid,
                   FX_DEFAULT_SOCKET_PATH_SUFFIX);
    ck_assert_str_eq(cfg->socket_path, expected);

    talloc_free(ctx);
    clear_env();
}
END_TEST

START_TEST(test_env_db_path) {
    clear_env();
    setenv("FANDEX_DB_PATH", "/tmp/test.db", 1);

    TALLOC_CTX *ctx = talloc_new(NULL);
    fx_cfg_t *cfg = talloc_zero(ctx, fx_cfg_t);
    res_t r = fx_cfg_env_load(cfg);
    ck_assert(!r.is_err);

    ck_assert_str_eq(cfg->db_path, "/tmp/test.db");

    const char *home = getenv("HOME");
    char expected[512];

    (void)snprintf(expected, sizeof(expected), "%s%s", home, FX_DEFAULT_WATCH_PATH_SUFFIX);
    ck_assert_str_eq(cfg->watch_path, expected);

    uid_t uid = getuid();
    (void)snprintf(expected, sizeof(expected), "/run/user/%u%s", (unsigned)uid,
                   FX_DEFAULT_SOCKET_PATH_SUFFIX);
    ck_assert_str_eq(cfg->socket_path, expected);

    talloc_free(ctx);
    clear_env();
}
END_TEST

START_TEST(test_env_socket_path) {
    clear_env();
    setenv("FANDEX_SOCKET_PATH", "/tmp/test.sock", 1);

    TALLOC_CTX *ctx = talloc_new(NULL);
    fx_cfg_t *cfg = talloc_zero(ctx, fx_cfg_t);
    res_t r = fx_cfg_env_load(cfg);
    ck_assert(!r.is_err);

    ck_assert_str_eq(cfg->socket_path, "/tmp/test.sock");

    const char *home = getenv("HOME");
    char expected[512];

    (void)snprintf(expected, sizeof(expected), "%s%s", home, FX_DEFAULT_WATCH_PATH_SUFFIX);
    ck_assert_str_eq(cfg->watch_path, expected);

    (void)snprintf(expected, sizeof(expected), "%s%s", home, FX_DEFAULT_DB_PATH_SUFFIX);
    ck_assert_str_eq(cfg->db_path, expected);

    talloc_free(ctx);
    clear_env();
}
END_TEST

START_TEST(test_env_all_three) {
    setenv("FANDEX_WATCH_PATH",  "/tmp/w", 1);
    setenv("FANDEX_DB_PATH",     "/tmp/d", 1);
    setenv("FANDEX_SOCKET_PATH", "/tmp/s", 1);

    TALLOC_CTX *ctx = talloc_new(NULL);
    fx_cfg_t *cfg = talloc_zero(ctx, fx_cfg_t);
    res_t r = fx_cfg_env_load(cfg);
    ck_assert(!r.is_err);

    ck_assert_str_eq(cfg->watch_path,  "/tmp/w");
    ck_assert_str_eq(cfg->db_path,     "/tmp/d");
    ck_assert_str_eq(cfg->socket_path, "/tmp/s");

    talloc_free(ctx);
    clear_env();
}
END_TEST

static Suite *config_env_suite(void)
{
    Suite *s = suite_create("config_env");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_env_no_vars);
    tcase_add_test(tc, test_env_watch_path);
    tcase_add_test(tc, test_env_db_path);
    tcase_add_test(tc, test_env_socket_path);
    tcase_add_test(tc, test_env_all_three);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    Suite *s = config_env_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_xml(sr, "reports/check/unit/config_env_test.xml");
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
