// libarchive null pointer safety -- null arguments (should FAIL to compile)
//
// Tests that DTT catches null pointer arguments to libarchive functions.
// All archive pointer parameters require _nonnull != 0.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: null archive pointer to support_filter_all
void variant_null_archive_ptr() {
    struct archive *a = 0;

    // DTT ERROR: requires arg0._nonnull != 0
    archive_read_support_filter_all(a);
}

// Variant 2: null entry pointer to set_pathname
void variant_null_entry_ptr() {
    struct archive_entry *entry = 0;

    // DTT ERROR: requires arg0._nonnull != 0
    archive_entry_set_pathname(entry, "file.txt");
}

int main() {
    variant_null_archive_ptr();
    variant_null_entry_ptr();
    return 0;
}
