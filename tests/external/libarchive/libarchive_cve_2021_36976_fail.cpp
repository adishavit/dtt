// CVE-2021-36976: Use-after-free in archive_entry copy_string (should FAIL to compile)
//
// CVE-2021-36976 is a use-after-free in libarchive's archive_entry copy_string
// functions. Accessing entry data after archive_entry_free leads to a heap
// use-after-free. DTT catches this because entry getters require @entry_alive != 0.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2021-36976

#include <archive.h>
#include <archive_entry.h>

// Variant 1: set_pathname then free then pathname (use-after-free)
void variant_use_after_free_pathname() {
    struct archive_entry *entry = archive_entry_new();       // @entry_alive = 1
    if (!entry) return;
    archive_entry_set_pathname(entry, "important_file.txt"); // requires @entry_alive != 0
    archive_entry_free(entry);                               // @entry_alive = 0

    // Requires @entry_alive != 0; this call must be rejected in enforce mode.
    // Accessing freed entry data -- this is the CVE-2021-36976 pattern
    const char *name = archive_entry_pathname(entry); // dtt-error {{requires @entry_alive != 0}}
    (void)name;
}

// Variant 2: set_size then free then size (use-after-free)
void variant_use_after_free_size() {
    struct archive_entry *entry = archive_entry_new();       // @entry_alive = 1
    if (!entry) return;
    archive_entry_set_size(entry, 4096);                     // requires @entry_alive != 0
    archive_entry_free(entry);                               // @entry_alive = 0

    // Requires @entry_alive != 0; this call must be rejected in enforce mode.
    archive_entry_size(entry); // dtt-error {{requires @entry_alive != 0}}
}

int main() {
    variant_use_after_free_pathname();
    variant_use_after_free_size();
    return 0;
}
