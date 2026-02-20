// libarchive write lifecycle -- write after close (should FAIL to compile)
//
// Tests that DTT catches archive_write_data called after archive_write_close.
// After close, @archive_state == 5, but archive_write_data requires >= 3.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: write_data after close
void variant_write_after_close() {
    struct archive *a = archive_write_new();          // @archive_state = 1
    archive_write_set_format_ustar(a);                // @archive_state = 2
    archive_write_open_filename(a, "output.tar");     // @archive_state = 3
    archive_write_close(a);                           // @archive_state = 5

    const char data[] = "should not be written";
    // DTT ERROR: requires arg0.@archive_state >= 3, but @archive_state == 5
    archive_write_data(a, data, sizeof(data));

    archive_write_free(a);                            // @archive_state = 0
}

int main() {
    variant_write_after_close();
    return 0;
}
