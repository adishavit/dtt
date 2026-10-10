// libarchive entry double-free -- free entry twice (should FAIL to compile)
//
// Tests that DTT catches double-free on archive entries.
// After the first free, @entry_alive == 0, and the second free
// requires @entry_alive != 0.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: double free on entry
void variant_double_free() {
    struct archive_entry *entry = archive_entry_new();    // @entry_alive = 1
    if (!entry) return;
    archive_entry_set_pathname(entry, "file.txt");
    archive_entry_free(entry);                            // @entry_alive = 0

    // Requires @entry_alive != 0; this call must be rejected in enforce mode.
    archive_entry_free(entry); // dtt-error {{requires @entry_alive != 0}}
}

int main() {
    variant_double_free();
    return 0;
}
