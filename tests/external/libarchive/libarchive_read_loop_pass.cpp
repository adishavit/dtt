// libarchive read loop -- correct usage patterns (should COMPILE)
//
// Tests the common pattern of iterating over archive entries in a loop,
// reading header then data for each entry.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: new -> support_filter_all -> open -> (next_header -> read_data) loop -> close -> free
void variant_read_loop() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    archive_read_support_filter_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "test.tar", 10240); // @archive_state = 3

    struct archive_entry *entry;
    char buf[1024];

    // Simulated loop iteration: read multiple entries
    archive_read_next_header(a, &entry);              // @archive_state = 4
    archive_read_data(a, buf, sizeof(buf));           // requires @archive_state >= 3
    (void)buf;

    archive_read_next_header(a, &entry);              // @archive_state = 4
    archive_read_data(a, buf, sizeof(buf));           // requires @archive_state >= 3

    archive_read_next_header(a, &entry);              // @archive_state = 4
    archive_read_data_skip(a);                        // requires @archive_state >= 3

    archive_read_close(a);                            // @archive_state = 5
    archive_read_free(a);                             // @archive_state = 0
    // scope_exit: @archive_state == 0 -- satisfied
}

int main() {
    variant_read_loop();
    return 0;
}
