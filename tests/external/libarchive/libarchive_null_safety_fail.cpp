// libarchive null pointer safety -- null arguments (should FAIL to compile)
//
// Tests that DTT catches null pointer arguments to libarchive functions.
// All archive pointer parameters require _nonnull != 0.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: null archive pointer to support_filter_all
void variant_null_archive_ptr() {
    struct archive *a = 0;

    // Requires _nonnull != 0; this call must be rejected in enforce mode.
    archive_read_support_filter_all(a); // dtt-error {{requires _nonnull != 0}}
}

// Variant 2: null entry pointer to set_pathname
void variant_null_entry_ptr() {
    struct archive_entry *entry = 0;

    // Requires _nonnull != 0; this call must be rejected in enforce mode.
    archive_entry_set_pathname(entry, "file.txt"); // dtt-error {{requires _nonnull != 0}}
}

int main() {
    variant_null_archive_ptr();
    variant_null_entry_ptr();
    return 0;
}
