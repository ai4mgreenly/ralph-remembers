#include "config/config_args.h"
#include "config/config_env.h"
#include "config/config.h"
#include <talloc.h>

#include <check.h>
#include <stdlib.h>
#include <string.h>

/* Helper: load a fresh cfg under a new top-level talloc context. */
static fx_cfg_t *base_cfg(void)
{
    unsetenv("FANDEX_WATCH_PATH");
    unsetenv("FANDEX_DB_PATH");
    unsetenv("FANDEX_SOCKET_PATH");
    fx_cfg_t *cfg = talloc_zero(NULL, fx_cfg_t);
    if (cfg) {
        fx_cfg_env_load(cfg);
    }
    return cfg;
}

START_TEST(test_args_empty) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    char *w = talloc_strdup(cfg, cfg->watch_path);
    char *d = talloc_strdup(cfg, cfg->db_path);
    char *s = talloc_strdup(cfg, cfg->socket_path);

    const char *argv[] = { "fandex" };
    res_t r = fx_cfg_args_apply(cfg, 1, argv);

    ck_assert(!r.is_err);
    ck_assert(!cfg->help);
    ck_assert_str_eq(cfg->watch_path,  w);
    ck_assert_str_eq(cfg->db_path,     d);
    ck_assert_str_eq(cfg->socket_path, s);

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_help_long) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "--help" };
    res_t r = fx_cfg_args_apply(cfg, 2, argv);

    ck_assert(!r.is_err);
    ck_assert(cfg->help);

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_help_short) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "-h" };
    res_t r = fx_cfg_args_apply(cfg, 2, argv);

    ck_assert(!r.is_err);
    ck_assert(cfg->help);

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_watch) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "--watch", "/tmp/w" };
    res_t r = fx_cfg_args_apply(cfg, 3, argv);

    ck_assert(!r.is_err);
    ck_assert_str_eq(cfg->watch_path, "/tmp/w");

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_db) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "--db", "/tmp/d" };
    res_t r = fx_cfg_args_apply(cfg, 3, argv);

    ck_assert(!r.is_err);
    ck_assert_str_eq(cfg->db_path, "/tmp/d");

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_socket) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "--socket", "/tmp/s" };
    res_t r = fx_cfg_args_apply(cfg, 3, argv);

    ck_assert(!r.is_err);
    ck_assert_str_eq(cfg->socket_path, "/tmp/s");

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_all_three) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = {
        "fandex",
        "--watch",  "/tmp/w",
        "--db",     "/tmp/d",
        "--socket", "/tmp/s"
    };
    res_t r = fx_cfg_args_apply(cfg, 7, argv);

    ck_assert(!r.is_err);
    ck_assert_str_eq(cfg->watch_path,  "/tmp/w");
    ck_assert_str_eq(cfg->db_path,     "/tmp/d");
    ck_assert_str_eq(cfg->socket_path, "/tmp/s");

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_unknown_flag) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "--bogus" };
    res_t r = fx_cfg_args_apply(cfg, 2, argv);

    ck_assert(r.is_err);

    fx_cfg_free(cfg);
}
END_TEST

START_TEST(test_args_missing_value) {
    fx_cfg_t *cfg = base_cfg();
    ck_assert_ptr_nonnull(cfg);

    const char *argv[] = { "fandex", "--watch" };
    res_t r = fx_cfg_args_apply(cfg, 2, argv);

    ck_assert(r.is_err);

    fx_cfg_free(cfg);
}
END_TEST

static Suite *config_args_suite(void)
{
    Suite *s = suite_create("config_args");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_args_empty);
    tcase_add_test(tc, test_args_help_long);
    tcase_add_test(tc, test_args_help_short);
    tcase_add_test(tc, test_args_watch);
    tcase_add_test(tc, test_args_db);
    tcase_add_test(tc, test_args_socket);
    tcase_add_test(tc, test_args_all_three);
    tcase_add_test(tc, test_args_unknown_flag);
    tcase_add_test(tc, test_args_missing_value);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    Suite *s = config_args_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_xml(sr, "reports/check/unit/config_args_test.xml");
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
