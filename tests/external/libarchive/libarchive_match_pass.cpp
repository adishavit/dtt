// libarchive match/pattern functions -- correct usage (should COMPILE)
//
// Exercises ALL archive_match_* functions annotated in libarchive.dtt,
// plus archive utility/query and error handling functions.

#include <archive.h>
#include <archive_entry.h>

// --- Match lifecycle ---

void variant_match_exclude() {
    struct archive *m = archive_match_new();               // @archive_state = 1
    archive_match_exclude_pattern(m, "*.tmp");
    archive_match_free(m);                                 // @archive_state = 0
}

void variant_match_include() {
    struct archive *m = archive_match_new();
    archive_match_include_pattern(m, "*.cpp");
    archive_match_free(m);
}

void variant_match_path_excluded() {
    struct archive *m = archive_match_new();
    archive_match_exclude_pattern(m, "*.log");

    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, "output.log");
    int excluded = archive_match_path_excluded(m, e);
    (void)excluded;

    archive_entry_free(e);
    archive_match_free(m);
}

void variant_match_excluded() {
    struct archive *m = archive_match_new();
    archive_match_exclude_pattern(m, "*.bak");
    archive_match_include_pattern(m, "src/*");

    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, "src/main.cpp");
    int excl = archive_match_excluded(m, e);
    (void)excl;

    archive_entry_free(e);
    archive_match_free(m);
}

// --- Error handling functions ---

void variant_error_handling() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "nonexistent.tar", 10240);

    const char *err_str = archive_error_string(a);
    int err_no = archive_errno(a);
    (void)err_str;
    (void)err_no;

    archive_clear_error(a);
    archive_set_error(a, -1, "test error: %s", "simulated");

    archive_read_close(a);
    archive_read_free(a);
}

// --- Utility / query functions ---

void variant_format_query() {
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_open_filename(a, "test.tar", 10240);

    int fmt = archive_format(a);
    const char *fmt_name = archive_format_name(a);
    (void)fmt;
    (void)fmt_name;

    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_query() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "test.tar.gz", 10240);

    int count = archive_filter_count(a);
    const char *fname = archive_filter_name(a, 0);
    long long fbytes = archive_filter_bytes(a, 0);
    (void)count;
    (void)fname;
    (void)fbytes;

    archive_read_close(a);
    archive_read_free(a);
}

void variant_compression_query() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "test.tar.gz", 10240);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    int comp = archive_compression(a);
    const char *comp_name = archive_compression_name(a);
#pragma clang diagnostic pop
    (void)comp;
    (void)comp_name;

    archive_read_close(a);
    archive_read_free(a);
}

void variant_file_count() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "test.tar", 10240);

    int fc = archive_file_count(a);
    (void)fc;

    archive_read_close(a);
    archive_read_free(a);
}

// --- Version functions (no arguments) ---

void variant_version() {
    int ver_num = archive_version_number();
    const char *ver_str = archive_version_string();
    const char *ver_det = archive_version_details();
    (void)ver_num;
    (void)ver_str;
    (void)ver_det;
}

int main() {
    variant_match_exclude();
    variant_match_include();
    variant_match_path_excluded();
    variant_match_excluded();
    variant_error_handling();
    variant_format_query();
    variant_filter_query();
    variant_compression_query();
    variant_file_count();
    variant_version();
    return 0;
}
