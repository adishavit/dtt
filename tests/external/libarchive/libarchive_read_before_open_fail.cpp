// libarchive read data before open -- wrong state (should FAIL to compile)
//
// Tests that DTT catches archive_read_data called on an archive that has been
// configured but not yet opened. @archive_state == 2 after configuration,
// but read_data requires >= 3.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: read_data on configured-but-not-opened archive
void variant_read_data_before_open() {
    struct archive *a = archive_read_new();               // @archive_state = 1
    if (!a) return;
    archive_read_support_filter_all(a);                   // @archive_state = 2

    char buf[1024];
    // Requires @archive_state >= 3; this call must be rejected in enforce mode.
    archive_read_data(a, buf, sizeof(buf)); // dtt-error {{requires @archive_state >= 3}}
    (void)buf;

    archive_read_free(a);
}

int main() {
    variant_read_data_before_open();
    return 0;
}
