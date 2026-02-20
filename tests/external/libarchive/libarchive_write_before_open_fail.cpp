// libarchive write data before open -- wrong state (should FAIL to compile)
//
// Tests that DTT catches archive_write_header and archive_write_data called
// on an archive that has been configured but not yet opened.
// @archive_state == 2 after configuration, but write operations require >= 3.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: write_header before open
void variant_write_header_before_open() {
    struct archive *a = archive_write_new();              // @archive_state = 1
    archive_write_set_format_ustar(a);                    // @archive_state = 2

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "file.txt");
    archive_entry_set_size(entry, 5);
    archive_entry_set_filetype(entry, 0100000);
    archive_entry_set_perm(entry, 0644);

    // DTT ERROR: requires arg0.@archive_state >= 3, but @archive_state == 2
    archive_write_header(a, entry);

    archive_entry_free(entry);
    archive_write_free(a);
}

int main() {
    variant_write_header_before_open();
    return 0;
}
