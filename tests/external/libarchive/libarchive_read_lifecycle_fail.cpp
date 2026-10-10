// libarchive read lifecycle -- read data after free (should FAIL to compile)
//
// Tests that DTT catches archive_read_data called after archive_read_free.
// After free, @archive_state == 0, but archive_read_data requires >= 3.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: read_data after free
void variant_read_after_free() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    if (!a) return;
    archive_read_support_filter_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "test.tar", 10240); // @archive_state = 3
    archive_read_close(a);                            // @archive_state = -1
    archive_read_free(a);                             // @archive_state = 0

    char buf[1024];
    // Requires @archive_state >= 3; this call must be rejected in enforce mode.
    archive_read_data(a, buf, sizeof(buf)); // dtt-error {{requires @archive_state >= 3}}
    (void)buf;
}

int main() {
    variant_read_after_free();
    return 0;
}
