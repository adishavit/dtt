// libarchive entry lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that the archive_entry_new -> setters/getters -> archive_entry_free
// lifecycle is tracked correctly by DTT ghost field @entry_alive.

#include <archive.h>
#include <archive_entry.h>

// Variant 1: entry_new -> set_pathname -> set_size -> set_filetype -> set_perm -> entry_free
void variant_entry_with_setters() {
    struct archive_entry *entry = archive_entry_new();  // @entry_alive = 1
    archive_entry_set_pathname(entry, "myfile.txt");    // requires @entry_alive != 0
    archive_entry_set_size(entry, 1024);                // requires @entry_alive != 0
    archive_entry_set_filetype(entry, 0100000);         // requires @entry_alive != 0
    archive_entry_set_perm(entry, 0644);                // requires @entry_alive != 0
    archive_entry_free(entry);                          // @entry_alive = 0
    // scope_exit: @entry_alive == 0 -- satisfied
}

// Variant 2: entry_new2 -> set_pathname -> entry_pathname (getter) -> entry_free
void variant_entry_new2_with_getter() {
    struct archive *a = archive_read_new();                 // needed for entry_new2
    struct archive_entry *entry = archive_entry_new2(a);    // @entry_alive = 1
    archive_entry_set_pathname(entry, "another_file.dat");  // requires @entry_alive != 0

    const char *name = archive_entry_pathname(entry);       // requires @entry_alive != 0
    (void)name;

    archive_entry_free(entry);                              // @entry_alive = 0
    archive_read_free(a);                                   // @archive_state = 0
}

int main() {
    variant_entry_with_setters();
    variant_entry_new2_with_getter();
    return 0;
}
