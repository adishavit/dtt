// libarchive skip configuration -- open directly after new (should FAIL to compile)
//
// Tests that DTT catches archive_read_open_filename called immediately after
// archive_read_new, skipping the required configuration step.
// @archive_state == 1 after new, but open requires == 2.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: open without any configure call
void variant_open_without_config() {
    struct archive *a = archive_read_new();           // @archive_state = 1

    // DTT ERROR: requires arg0.@archive_state == 2, but @archive_state == 1
    archive_read_open_filename(a, "test.tar", 10240);

    archive_read_close(a);
    archive_read_free(a);
}

int main() {
    variant_open_without_config();
    return 0;
}
