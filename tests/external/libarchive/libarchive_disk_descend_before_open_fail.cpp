// libarchive disk descend before open -- wrong state (should FAIL to compile)
//
// Tests that DTT catches archive_read_disk_descend called on a disk archive
// that has not been opened yet. descend requires @archive_state == 3,
// but after new the state is 1.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: descend on unopened disk archive
void variant_descend_before_open() {
    struct archive *disk = archive_read_disk_new();       // @archive_state = 1
    if (!disk) return;

    // Requires @archive_state == 3; this call must be rejected in enforce mode.
    archive_read_disk_descend(disk); // dtt-error {{requires @archive_state == 3}}

    archive_read_free(disk);
}

int main() {
    variant_descend_before_open();
    return 0;
}
