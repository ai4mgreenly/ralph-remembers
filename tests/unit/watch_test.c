#include "watch/watch.h"

#include "log/log.h"

#include <check.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fanotify.h>
#include <talloc.h>
#include <unistd.h>

// ---- Mock state for POSIX wrappers ----

static int32_t mock_fanotify_init_retval = -1;
static int32_t mock_fanotify_mark_retval = -1;
static int32_t mock_open_retval = 10;
static int32_t mock_close_called_fd = -1;
static int32_t mock_close_retval = 0;

int32_t posix_fanotify_init_(uint32_t flags, uint32_t event_f_flags);
int32_t posix_fanotify_mark_(int32_t fanotify_fd, uint32_t flags, uint64_t mask, int32_t dirfd, const char *pathname);
int32_t posix_open_(const char *pathname, int32_t flags);
int32_t posix_close_(int32_t fd);

int32_t posix_fanotify_init_(uint32_t flags, uint32_t event_f_flags)
{
    (void)flags;
    (void)event_f_flags;
    return mock_fanotify_init_retval;
}

int32_t posix_fanotify_mark_(int32_t fanotify_fd, uint32_t flags, uint64_t mask,
                             int32_t dirfd, const char *pathname)
{
    (void)fanotify_fd;
    (void)flags;
    (void)mask;
    (void)dirfd;
    (void)pathname;
    return mock_fanotify_mark_retval;
}

int32_t posix_open_(const char *pathname, int32_t flags)
{
    (void)pathname;
    (void)flags;
    return mock_open_retval;
}

int32_t posix_close_(int32_t fd)
{
    mock_close_called_fd = fd;
    return mock_close_retval;
}

// ---- Helpers ----

static char log_buf[4096];

static const char *read_log(FILE *f)
{
    rewind(f);
    size_t n = fread(log_buf, 1, sizeof(log_buf) - 1, f);
    log_buf[n] = '\0';
    rewind(f);
    (void)ftruncate(fileno(f), 0);
    return log_buf;
}

// ---- fx_watch_init: success ----

START_TEST(test_watch_init_success) {
    TALLOC_CTX *ctx = talloc_new(NULL);
    FILE *f = tmpfile();
    ck_assert_ptr_nonnull(f);

    fx_log_t *log = fx_log_init(ctx, f, FX_LOG_DEBUG);

    mock_fanotify_init_retval = 42;
    mock_open_retval = 10;
    mock_fanotify_mark_retval = 0;
    mock_close_called_fd = -1;

    res_t res = fx_watch_init(ctx, log, "/home/user/projects");
    ck_assert(!res.is_err);
    ck_assert_ptr_nonnull(res.ok);

    const char *out = read_log(f);
    ck_assert_ptr_nonnull(strstr(out, "watch init"));

    fx_watch_free(res.ok);
    fclose(f);
    talloc_free(ctx);
}
END_TEST

// ---- fx_watch_init: fanotify_init fails ----

START_TEST(test_watch_init_fanotify_init_fails) {
    TALLOC_CTX *ctx = talloc_new(NULL);
    FILE *f = tmpfile();
    ck_assert_ptr_nonnull(f);

    fx_log_t *log = fx_log_init(ctx, f, FX_LOG_DEBUG);

    mock_fanotify_init_retval = -1;
    mock_open_retval = 10;
    mock_fanotify_mark_retval = 0;
    mock_close_called_fd = -1;

    res_t res = fx_watch_init(ctx, log, "/home/user/projects");
    ck_assert(res.is_err);
    ck_assert_int_eq((int)res.err->code, (int)ERR_IO);

    fclose(f);
    talloc_free(ctx);
}
END_TEST

// ---- fx_watch_init: fanotify_mark fails, closes fd ----

// ---- fx_watch_init: open watch_path fails, closes fan_fd ----

START_TEST(test_watch_init_open_fails) {
    TALLOC_CTX *ctx = talloc_new(NULL);
    FILE *f = tmpfile();
    ck_assert_ptr_nonnull(f);

    fx_log_t *log = fx_log_init(ctx, f, FX_LOG_DEBUG);

    mock_fanotify_init_retval = 42;
    mock_open_retval = -1;
    mock_fanotify_mark_retval = 0;
    mock_close_called_fd = -1;

    res_t res = fx_watch_init(ctx, log, "/home/user/projects");
    ck_assert(res.is_err);
    ck_assert_int_eq((int)res.err->code, (int)ERR_IO);
    ck_assert_int_eq(mock_close_called_fd, 42);

    fclose(f);
    talloc_free(ctx);
}
END_TEST

// ---- fx_watch_init: fanotify_mark fails, closes dir fd then fan_fd ----

START_TEST(test_watch_init_fanotify_mark_fails) {
    TALLOC_CTX *ctx = talloc_new(NULL);
    FILE *f = tmpfile();
    ck_assert_ptr_nonnull(f);

    fx_log_t *log = fx_log_init(ctx, f, FX_LOG_DEBUG);

    mock_fanotify_init_retval = 77;
    mock_open_retval = 10;
    mock_fanotify_mark_retval = -1;
    mock_close_called_fd = -1;

    res_t res = fx_watch_init(ctx, log, "/home/user/projects");
    ck_assert(res.is_err);
    ck_assert_int_eq((int)res.err->code, (int)ERR_IO);
    ck_assert_int_eq(mock_close_called_fd, 77);

    fclose(f);
    talloc_free(ctx);
}
END_TEST

// ---- fx_watch_free: closes fd and logs ----

START_TEST(test_watch_free) {
    TALLOC_CTX *ctx = talloc_new(NULL);
    FILE *f = tmpfile();
    ck_assert_ptr_nonnull(f);

    fx_log_t *log = fx_log_init(ctx, f, FX_LOG_DEBUG);

    mock_fanotify_init_retval = 55;
    mock_open_retval = 10;
    mock_fanotify_mark_retval = 0;
    mock_close_called_fd = -1;

    res_t res = fx_watch_init(ctx, log, "/home/user/projects");
    ck_assert(!res.is_err);

    // Clear log from init
    read_log(f);

    fx_watch_free(res.ok);

    ck_assert_int_eq(mock_close_called_fd, 55);
    const char *out = read_log(f);
    ck_assert_ptr_nonnull(strstr(out, "watch free"));

    fclose(f);
    talloc_free(ctx);
}
END_TEST

// ---- Path filtering ----

START_TEST(test_path_under_inside) {
    ck_assert(fx_watch_path_under("/home/projects", "/home/projects/foo.txt"));
}
END_TEST

START_TEST(test_path_under_exact) {
    ck_assert(fx_watch_path_under("/home/projects", "/home/projects"));
}
END_TEST

START_TEST(test_path_under_outside) {
    ck_assert(!fx_watch_path_under("/home/projects", "/home/other/foo.txt"));
}
END_TEST

START_TEST(test_path_under_prefix_no_slash) {
    // /home/projects-backup must not match /home/projects
    ck_assert(!fx_watch_path_under("/home/projects", "/home/projects-backup/foo.txt"));
}
END_TEST

START_TEST(test_path_under_trailing_slash_watch) {
    // Trailing slash on watch_path should still work
    ck_assert(fx_watch_path_under("/home/projects/", "/home/projects/foo.txt"));
}
END_TEST

START_TEST(test_path_under_nested) {
    ck_assert(fx_watch_path_under("/home/projects", "/home/projects/a/b/c"));
}
END_TEST

// ---- Event name mapping ----

START_TEST(test_event_name_create) {
    ck_assert_str_eq(fx_watch_event_name(FAN_CREATE), "create");
}
END_TEST

START_TEST(test_event_name_delete) {
    ck_assert_str_eq(fx_watch_event_name(FAN_DELETE), "delete");
}
END_TEST

START_TEST(test_event_name_modify) {
    ck_assert_str_eq(fx_watch_event_name(FAN_MODIFY), "modify");
}
END_TEST

START_TEST(test_event_name_moved_from) {
    ck_assert_str_eq(fx_watch_event_name(FAN_MOVED_FROM), "moved_from");
}
END_TEST

START_TEST(test_event_name_moved_to) {
    ck_assert_str_eq(fx_watch_event_name(FAN_MOVED_TO), "moved_to");
}
END_TEST

START_TEST(test_event_name_unknown) {
    ck_assert_ptr_null(fx_watch_event_name(0));
}
END_TEST

// ---- Suite wiring ----

static Suite *watch_suite(void)
{
    Suite *s = suite_create("watch");

    TCase *tc_init = tcase_create("init");
    tcase_add_test(tc_init, test_watch_init_success);
    tcase_add_test(tc_init, test_watch_init_fanotify_init_fails);
    tcase_add_test(tc_init, test_watch_init_open_fails);
    tcase_add_test(tc_init, test_watch_init_fanotify_mark_fails);
    tcase_add_test(tc_init, test_watch_free);
    suite_add_tcase(s, tc_init);

    TCase *tc_path = tcase_create("path_filter");
    tcase_add_test(tc_path, test_path_under_inside);
    tcase_add_test(tc_path, test_path_under_exact);
    tcase_add_test(tc_path, test_path_under_outside);
    tcase_add_test(tc_path, test_path_under_prefix_no_slash);
    tcase_add_test(tc_path, test_path_under_trailing_slash_watch);
    tcase_add_test(tc_path, test_path_under_nested);
    suite_add_tcase(s, tc_path);

    TCase *tc_event = tcase_create("event_name");
    tcase_add_test(tc_event, test_event_name_create);
    tcase_add_test(tc_event, test_event_name_delete);
    tcase_add_test(tc_event, test_event_name_modify);
    tcase_add_test(tc_event, test_event_name_moved_from);
    tcase_add_test(tc_event, test_event_name_moved_to);
    tcase_add_test(tc_event, test_event_name_unknown);
    suite_add_tcase(s, tc_event);

    return s;
}

int main(void)
{
    Suite *s = watch_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_xml(sr, "reports/check/unit/watch_test.xml");
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
