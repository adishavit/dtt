// libarchive configure-before-open -- correct usage patterns (should COMPILE)
//
// Tests that configuring the archive before opening is properly tracked.
// Each variant uses a single configuration function (filter or format)
// before opening, as each transitions @archive_state from 1 to 2.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: new -> support_filter_all -> open_filename -> next_header -> close -> free
void variant_filter_then_open() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    archive_read_support_filter_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "data.tar", 10240); // @archive_state = 3

    struct archive_entry *entry;
    archive_read_next_header(a, &entry);              // @archive_state = 4

    archive_read_close(a);                            // @archive_state = 5
    archive_read_free(a);                             // @archive_state = 0
    // scope_exit: @archive_state == 0 -- satisfied
}

// Variant 2: new -> support_format_tar -> open_filename -> read_data_skip -> close -> free
void variant_format_tar_then_open() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    archive_read_support_format_tar(a);               // @archive_state = 2
    archive_read_open_filename(a, "data.tar", 10240); // @archive_state = 3

    archive_read_data_skip(a);                        // requires @archive_state >= 3

    archive_read_close(a);                            // @archive_state = 5
    archive_read_free(a);                             // @archive_state = 0
}

int main() {
    variant_filter_then_open();
    variant_format_tar_then_open();
    return 0;
}
