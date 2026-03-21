#include <check.h>
#include <stdlib.h>

START_TEST(test_hello) {
    ck_assert_int_eq(1, 1);
}
END_TEST

static Suite *hello_suite(void)
{
    Suite *s = suite_create("hello");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_hello);
    suite_add_tcase(s, tc);

    return s;
}

int main(void)
{
    Suite *s = hello_suite();
    SRunner *sr = srunner_create(s);

    srunner_set_xml(sr, "reports/check/unit/hello_test.xml");
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
