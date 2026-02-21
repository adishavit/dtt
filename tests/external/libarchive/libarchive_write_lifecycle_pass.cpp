// libarchive write lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that the archive_write_new -> configure -> open -> write -> close -> free
// lifecycle is tracked correctly by DTT ghost field @archive_state.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: new -> set_format_ustar -> open_filename -> write_header + write_data -> close -> free
void variant_write_ustar() {
    struct archive *a = archive_write_new();              // @archive_state = 1
    archive_write_set_format_ustar(a);                    // @archive_state = 2
    archive_write_open_filename(a, "output.tar");         // @archive_state = 3

    struct archive_entry *entry = archive_entry_new();    // @entry_alive = 1
    archive_entry_set_pathname(entry, "file.txt");
    archive_entry_set_size(entry, 5);
    archive_entry_set_filetype(entry, 0100000);
    archive_entry_set_perm(entry, 0644);

    archive_write_header(a, entry);                       // requires @archive_state >= 3

    const char data[] = "hello";
    archive_write_data(a, data, sizeof(data) - 1);        // requires @archive_state >= 3

    archive_entry_free(entry);                            // @entry_alive = 0

    archive_write_close(a);                               // @archive_state = 5
    archive_write_free(a);                                // @archive_state = 0
    // scope_exit: @archive_state == 0 -- satisfied
}

// Variant 2: new -> add_filter_gzip -> open_filename -> write_header -> finish_entry -> close -> free
void variant_write_gzip() {
    struct archive *a = archive_write_new();              // @archive_state = 1
    archive_write_add_filter_gzip(a);                     // @archive_state = 2
    archive_write_open_filename(a, "output.tar.gz");      // @archive_state = 3

    struct archive_entry *entry = archive_entry_new();    // @entry_alive = 1
    archive_entry_set_pathname(entry, "data.bin");
    archive_entry_set_size(entry, 0);
    archive_entry_set_filetype(entry, 0100000);
    archive_entry_set_perm(entry, 0644);

    archive_write_header(a, entry);                       // requires @archive_state >= 3
    archive_write_finish_entry(a);                        // requires @archive_state >= 3

    archive_entry_free(entry);                            // @entry_alive = 0

    archive_write_close(a);                               // @archive_state = 5
    archive_write_free(a);                                // @archive_state = 0
}

int main() {
    variant_write_ustar();
    variant_write_gzip();
    return 0;
}
