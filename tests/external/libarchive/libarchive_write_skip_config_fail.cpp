// libarchive write skip configuration -- open directly after new (should FAIL to compile)
//
// Tests that DTT catches archive_write_open_filename called immediately after
// archive_write_new without configuring format/filter first.
// @archive_state == 1 after new, but open requires == 2.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: write open without format/filter configuration
void variant_write_open_without_config() {
    struct archive *a = archive_write_new();              // @archive_state = 1

    // DTT ERROR: requires arg0.@archive_state == 2, but @archive_state == 1
    archive_write_open_filename(a, "output.tar");

    archive_write_close(a);
    archive_write_free(a);
}

int main() {
    variant_write_open_without_config();
    return 0;
}
