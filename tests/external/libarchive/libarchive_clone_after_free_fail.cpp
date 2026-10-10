// libarchive clone after free -- clone a freed entry (should FAIL to compile)
//
// Tests that DTT catches archive_entry_clone called on an entry that
// has already been freed. clone requires @entry_alive != 0.
// This is the CVE-2021-36976 pattern applied to clone.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: clone after free
void variant_clone_after_free() {
    struct archive_entry *orig = archive_entry_new();     // @entry_alive = 1
    if (!orig) return;
    archive_entry_set_pathname(orig, "original.txt");
    archive_entry_free(orig);                             // @entry_alive = 0

    // Requires @entry_alive != 0; this call must be rejected in enforce mode.
    struct archive_entry *clone = archive_entry_clone(orig); // dtt-error {{requires @entry_alive != 0}}
    (void)clone;
}

int main() {
    variant_clone_after_free();
    return 0;
}
