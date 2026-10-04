#include <glib.h>

#include <src/fsearch_rename_engine.h>

static char *
apply(const char *search,
      const char *replace,
      bool match_case,
      bool diacritics,
      bool use_regex,
      bool ignore_extension,
      int64_t counter,
      const char *old_name) {
    FsearchRenameOptions options = {.search = (char *)search,
                                    .replace = (char *)replace,
                                    .match_case = match_case,
                                    .diacritics = diacritics,
                                    .use_regex = use_regex,
                                    .ignore_extension = ignore_extension,
                                    .counter_start = 1};

    FsearchRenameEngine *engine = fsearch_rename_engine_new(&options, NULL);
    g_assert_nonnull(engine);

    char *result = fsearch_rename_engine_apply(engine, old_name, counter);
    fsearch_rename_engine_free(engine);
    return result;
}

static void
test_rename_literal_case_insensitive(void) {
    g_assert_cmpstr(apply("foo", "bar", false, false, false, false, 1, "foo_file.txt"), ==, "bar_file.txt");
    g_assert_cmpstr(apply("FOO", "bar", false, false, false, false, 1, "x_foo_y_FOO_z"), ==, "x_bar_y_bar_z");
    g_assert_cmpstr(apply("不存在的字符串", "x", false, false, false, false, 1, "中文名.txt"), ==, "中文名.txt");
}

static void
test_rename_literal_match_case(void) {
    g_assert_cmpstr(apply("FOO", "bar", true, false, false, false, 1, "foo_FOO.txt"), ==, "foo_bar.txt");
    g_assert_cmpstr(apply("foo", "bar", true, false, false, false, 1, "FOO.txt"), ==, "FOO.txt");
}

static void
test_rename_literal_replace_all(void) {
    g_assert_cmpstr(apply("_", "-", false, false, false, false, 1, "a_b_c"), ==, "a-b-c");
}

static void
test_rename_regex_backreference(void) {
    g_assert_cmpstr(apply("^(.*)\\.pdf$", "\\1_备份.pdf", false, false, true, false, 1, "报告.pdf"), ==, "报告_备份.pdf");
    g_assert_cmpstr(apply("([0-9]+)x([0-9]+)", "\\2x\\1", false, false, true, false, 1, "photo_1920x1080.png"),
                    ==,
                    "photo_1080x1920.png");
}

static void
test_rename_regex_case_insensitive_default(void) {
    g_assert_cmpstr(apply("ABC", "x", false, false, true, false, 1, "abcdef"), ==, "xdef");
    g_assert_cmpstr(apply("ABC", "x", true, false, true, false, 1, "abcdef"), ==, "abcdef");
}

static void
test_rename_counter(void) {
    g_assert_cmpstr(apply("^", "#_", false, false, true, false, 1, "a.txt"), ==, "1_a.txt");
    g_assert_cmpstr(apply("^", "#_", false, false, true, false, 41, "b.txt"), ==, "41_b.txt");
    // `##` is a literal `#`
    g_assert_cmpstr(apply("^", "##_", false, false, true, false, 7, "c.txt"), ==, "#_c.txt");
    // counter without regex
    g_assert_cmpstr(apply("song", "# song", false, false, false, false, 3, "song.mp3"), ==, "3 song.mp3");
}

static void
test_rename_ignore_extension(void) {
    g_assert_cmpstr(apply("report", "Report", false, false, false, true, 1, "report_2026.txt"), ==,
                    "Report_2026.txt");
    // the extension itself is not touched
    g_assert_cmpstr(apply("txt", "md", false, false, false, true, 1, "notes.txt"), ==, "notes.txt");
    // without ignore_extension the extension is replaced
    g_assert_cmpstr(apply("txt", "md", false, false, false, false, 1, "notes.txt"), ==, "notes.md");
    // hidden files have no extension
    g_assert_cmpstr(apply("hidden", "shown", false, false, false, true, 1, ".hidden"), ==, ".shown");
}

static void
test_rename_diacritics(void) {
    g_assert_cmpstr(apply("cafe", "café", false, false, false, false, 1, "café_menu.txt"), ==, "café_menu.txt");
    g_assert_cmpstr(apply("cafe", "Kaffee", false, true, false, false, 1, "café_menu.txt"), ==, "Kaffee_menu.txt");
    g_assert_cmpstr(apply("über", "ue", false, true, false, false, 1, "Über.txt"), ==, "ue.txt");
}

static void
test_rename_invalid_regex(void) {
    FsearchRenameOptions options = {.search = "(unclosed",
                                    .replace = "",
                                    .match_case = false,
                                    .diacritics = false,
                                    .use_regex = true,
                                    .ignore_extension = false,
                                    .counter_start = 1};
    GError *error = NULL;
    FsearchRenameEngine *engine = fsearch_rename_engine_new(&options, &error);
    g_assert_null(engine);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

static void
test_rename_check(void) {
    g_assert_cmpuint(fsearch_rename_engine_check("valid_name.txt"), ==, FSEARCH_RENAME_STATUS_OK);
    g_assert_cmpuint(fsearch_rename_engine_check(""), ==, FSEARCH_RENAME_STATUS_EMPTY);
    g_assert_cmpuint(fsearch_rename_engine_check("a/b"), ==, FSEARCH_RENAME_STATUS_INVALID_CHARS);
    g_assert_cmpuint(fsearch_rename_engine_check("bad\tname"), ==, FSEARCH_RENAME_STATUS_INVALID_CHARS);
}

static void
test_rename_empty_search(void) {
    g_assert_cmpstr(apply("", "", false, false, false, false, 1, "unchanged.txt"), ==, "unchanged.txt");
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/rename/literal_case_insensitive", test_rename_literal_case_insensitive);
    g_test_add_func("/FSearch/rename/literal_match_case", test_rename_literal_match_case);
    g_test_add_func("/FSearch/rename/literal_replace_all", test_rename_literal_replace_all);
    g_test_add_func("/FSearch/rename/regex_backreference", test_rename_regex_backreference);
    g_test_add_func("/FSearch/rename/regex_case", test_rename_regex_case_insensitive_default);
    g_test_add_func("/FSearch/rename/counter", test_rename_counter);
    g_test_add_func("/FSearch/rename/ignore_extension", test_rename_ignore_extension);
    g_test_add_func("/FSearch/rename/diacritics", test_rename_diacritics);
    g_test_add_func("/FSearch/rename/invalid_regex", test_rename_invalid_regex);
    g_test_add_func("/FSearch/rename/check", test_rename_check);
    g_test_add_func("/FSearch/rename/empty_search", test_rename_empty_search);
    return g_test_run();
}
