// libarchive entry use-after-free -- access entry after free (should FAIL to compile)
//
// Tests that DTT catches archive_entry_pathname called after archive_entry_free.
// After free, @entry_alive == 0, but getters require @entry_alive != 0.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: pathname getter after free
void variant_get_pathname_after_free() {
    struct archive_entry *entry = archive_entry_new();     // @entry_alive = 1
    archive_entry_free(entry);                             // @entry_alive = 0

    // DTT ERROR: requires arg0.@entry_alive != 0, but @entry_alive == 0
    const char *name = archive_entry_pathname(entry);
    (void)name;
}

int main() {
    variant_get_pathname_after_free();
    return 0;
}
