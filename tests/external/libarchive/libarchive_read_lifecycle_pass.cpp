// libarchive read lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that the archive_read_new -> configure -> open -> iterate -> close -> free
// lifecycle is tracked correctly by DTT ghost field @archive_state.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: new -> support_filter_all -> open_filename -> next_header -> read_data -> close -> free
void variant_read_with_filter() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    archive_read_support_filter_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "test.tar", 10240); // @archive_state = 3

    struct archive_entry *entry;
    archive_read_next_header(a, &entry);              // @archive_state = 4

    char buf[1024];
    archive_read_data(a, buf, sizeof(buf));           // requires @archive_state >= 3
    (void)buf;

    archive_read_close(a);                            // @archive_state = 5
    archive_read_free(a);                             // @archive_state = 0
    // scope_exit: @archive_state == 0 -- satisfied
}

// Variant 2: new -> support_format_all -> open_filename -> next_header -> read_data_skip -> close -> free
void variant_read_with_format() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    archive_read_support_format_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "test.tar", 10240); // @archive_state = 3

    struct archive_entry *entry;
    archive_read_next_header(a, &entry);              // @archive_state = 4
    archive_read_data_skip(a);                        // requires @archive_state >= 3

    archive_read_close(a);                            // @archive_state = 5
    archive_read_free(a);                             // @archive_state = 0
}

int main() {
    variant_read_with_filter();
    variant_read_with_format();
    return 0;
}
