// libarchive disk read/write -- correct usage (should COMPILE)
//
// Exercises ALL archive_read_disk_* and archive_write_disk_* functions
// annotated in libarchive.dtt.
//
// Note: archive_write_disk_new creates state=1 with no annotated open
// transition, so write disk config functions are exercised without
// attempting write_header/write_data (which require state >= 3).

#include <archive.h>
#include <archive_entry.h>

// --- Read disk variants ---

void variant_read_disk_basic() {
    struct archive *disk = archive_read_disk_new();          // @archive_state = 1
    archive_read_disk_set_standard_lookup(disk);
    archive_read_disk_open(disk, "/tmp");                    // @archive_state = 3
    archive_read_disk_descend(disk);                         // requires state == 3
    archive_read_close(disk);                                // @archive_state = 5
    archive_read_free(disk);                                 // @archive_state = 0
}

void variant_read_disk_symlink_physical() {
    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_symlink_physical(disk);
    archive_read_disk_open(disk, "/tmp");
    archive_read_close(disk);
    archive_read_free(disk);
}

void variant_read_disk_symlink_logical() {
    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_symlink_logical(disk);
    archive_read_disk_open(disk, "/tmp");
    archive_read_close(disk);
    archive_read_free(disk);
}

void variant_read_disk_symlink_hybrid() {
    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_symlink_hybrid(disk);
    archive_read_disk_open(disk, "/tmp");
    archive_read_close(disk);
    archive_read_free(disk);
}

void variant_read_disk_open_w() {
    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);
    archive_read_disk_open_w(disk, L"/tmp");                 // wide-char variant
    archive_read_close(disk);
    archive_read_free(disk);
}

void variant_read_disk_entry_from_file() {
    struct archive *disk = archive_read_disk_new();
    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "/tmp/testfile");

    // fd=-1 and stat=NULL lets libarchive stat the pathname itself
    archive_read_disk_entry_from_file(disk, entry, -1, 0);

    archive_entry_free(entry);
    archive_read_free(disk);
}

// --- Write disk config variants ---

void variant_write_disk_config() {
    struct archive *ext = archive_write_disk_new();          // @archive_state = 1
    archive_write_disk_set_options(ext, 0);
    archive_write_disk_set_standard_lookup(ext);
    archive_write_free(ext);                                 // @archive_state = 0
}

// --- Full disk-to-archive workflow ---
// Reads from disk, writes into a tar archive (standard write path)

void variant_disk_to_archive() {
    // Set up disk reader
    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);
    archive_read_disk_set_symlink_physical(disk);
    archive_read_disk_open(disk, "/tmp");

    // Set up archive writer (regular archive, not disk)
    struct archive *out = archive_write_new();
    archive_write_set_format_ustar(out);
    archive_write_open_filename(out, "from_disk.tar");

    // Write one entry to the archive
    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "file_from_disk.txt");
    archive_entry_set_size(entry, 4);
    archive_entry_set_filetype(entry, 0100000);
    archive_entry_set_perm(entry, 0644);

    archive_write_header(out, entry);
    const char data[] = "data";
    archive_write_data(out, data, 4);
    archive_write_finish_entry(out);

    archive_entry_free(entry);

    // Clean up both
    archive_read_close(disk);
    archive_read_free(disk);
    archive_write_close(out);
    archive_write_free(out);
}

int main() {
    variant_read_disk_basic();
    variant_read_disk_symlink_physical();
    variant_read_disk_symlink_logical();
    variant_read_disk_symlink_hybrid();
    variant_read_disk_open_w();
    variant_read_disk_entry_from_file();
    variant_write_disk_config();
    variant_disk_to_archive();
    return 0;
}
