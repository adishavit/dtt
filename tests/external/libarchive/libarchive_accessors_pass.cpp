// libarchive entry accessors -- correct usage (should COMPILE)
//
// Exercises ALL archive_entry_* setter and getter functions annotated in
// libarchive.dtt, including pathname variants, time accessors, ownership
// fields, link functions, stat/sourcepath, clone/clear, and encrypted checks.

#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>

// --- Pathname setter/getter variants ---

void variant_pathname_utf8() {
    struct archive_entry *e = archive_entry_new();         // @entry_alive = 1
    archive_entry_set_pathname_utf8(e, "utf8_file.txt");
    const char *p = archive_entry_pathname_utf8(e);
    (void)p;
    archive_entry_free(e);                                 // @entry_alive = 0
}

void variant_pathname_w() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_copy_pathname_w(e, L"wide_file.txt");
    const wchar_t *p = archive_entry_pathname_w(e);
    (void)p;
    archive_entry_free(e);
}

void variant_copy_pathname() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_copy_pathname(e, "copied_name.txt");
    const char *p = archive_entry_pathname(e);
    (void)p;
    archive_entry_free(e);
}

// --- Size accessors ---

void variant_size_is_set() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_size(e, 1024);
    int has_size = archive_entry_size_is_set(e);
    long long sz = archive_entry_size(e);
    (void)has_size;
    (void)sz;
    archive_entry_free(e);
}

// --- Filetype and permission accessors ---

void variant_filetype_perm() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_filetype(e, 0100000);  // regular file
    archive_entry_set_perm(e, 0755);
    unsigned ft = archive_entry_filetype(e);
    unsigned pm = archive_entry_perm(e);
    (void)ft;
    (void)pm;
    archive_entry_free(e);
}

// --- Time accessors (mtime, atime, ctime) ---

void variant_mtime() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_mtime(e, 1700000000, 0);
    long mt = archive_entry_mtime(e);
    (void)mt;
    archive_entry_free(e);
}

void variant_atime() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_atime(e, 1700000000, 0);
    long at = archive_entry_atime(e);
    (void)at;
    archive_entry_free(e);
}

void variant_ctime() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_ctime(e, 1700000000, 0);
    long ct = archive_entry_ctime(e);
    (void)ct;
    archive_entry_free(e);
}

// --- Ownership accessors (uid, gid, uname, gname) ---

void variant_uid() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_uid(e, 1000);
    long uid = archive_entry_uid(e);
    (void)uid;
    archive_entry_free(e);
}

void variant_gid() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_gid(e, 1000);
    long gid = archive_entry_gid(e);
    (void)gid;
    archive_entry_free(e);
}

void variant_uname() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_uname(e, "user");
    const char *u = archive_entry_uname(e);
    (void)u;
    archive_entry_free(e);
}

void variant_gname() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_gname(e, "group");
    const char *g = archive_entry_gname(e);
    (void)g;
    archive_entry_free(e);
}

// --- Link accessors (hardlink, symlink, link) ---

void variant_hardlink() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_hardlink(e, "target.txt");
    const char *hl = archive_entry_hardlink(e);
    (void)hl;
    archive_entry_free(e);
}

void variant_symlink() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_symlink(e, "target_link");
    const char *sl = archive_entry_symlink(e);
    (void)sl;
    archive_entry_free(e);
}

void variant_set_link() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_link(e, "link_target");
    archive_entry_free(e);
}

// --- Sourcepath accessors ---

void variant_sourcepath() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_copy_sourcepath(e, "/src/path");
    const char *sp = archive_entry_sourcepath(e);
    (void)sp;
    archive_entry_free(e);
}

void variant_copy_sourcepath() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_copy_sourcepath(e, "/copy/src/path");
    const char *sp = archive_entry_sourcepath(e);
    (void)sp;
    archive_entry_free(e);
}

// --- Stat copy ---

void variant_copy_stat() {
    struct archive_entry *e = archive_entry_new();
    struct stat st;
    st.st_mode = 0100644;
    st.st_size = 42;
    archive_entry_copy_stat(e, &st);
    archive_entry_free(e);
}

// --- Clone and clear ---

void variant_clone() {
    struct archive_entry *orig = archive_entry_new();     // @entry_alive = 1
    archive_entry_set_pathname(orig, "original.txt");
    archive_entry_set_size(orig, 256);

    struct archive_entry *clone = archive_entry_clone(orig); // @entry_alive = 1 (new)
    const char *cloned_name = archive_entry_pathname(clone);
    (void)cloned_name;

    archive_entry_free(clone);                            // @entry_alive = 0
    archive_entry_free(orig);                             // @entry_alive = 0
}

void variant_clear() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, "will_be_cleared.txt");
    archive_entry_set_size(e, 100);
    archive_entry_clear(e);  // resets entry fields but keeps it alive
    archive_entry_free(e);
}

// --- Encryption checks ---

void variant_encryption_checks() {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, "encrypted.dat");
    int data_enc = archive_entry_is_data_encrypted(e);
    int meta_enc = archive_entry_is_metadata_encrypted(e);
    int any_enc  = archive_entry_is_encrypted(e);
    (void)data_enc;
    (void)meta_enc;
    (void)any_enc;
    archive_entry_free(e);
}

// --- All setters in one workflow ---

void variant_all_setters() {
    struct archive_entry *e = archive_entry_new();

    archive_entry_set_pathname(e, "full.txt");
    archive_entry_set_pathname_utf8(e, "full_utf8.txt");
    archive_entry_copy_pathname(e, "full_copy.txt");
    archive_entry_copy_pathname_w(e, L"full_wide.txt");
    archive_entry_set_size(e, 2048);
    archive_entry_set_filetype(e, 0100000);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_mtime(e, 1700000000, 0);
    archive_entry_set_atime(e, 1700000000, 0);
    archive_entry_set_ctime(e, 1700000000, 0);
    archive_entry_set_uid(e, 1000);
    archive_entry_set_gid(e, 1000);
    archive_entry_set_uname(e, "user");
    archive_entry_set_gname(e, "group");
    archive_entry_set_hardlink(e, "hl_target");
    archive_entry_set_symlink(e, "sl_target");
    archive_entry_set_link(e, "link_target");
    archive_entry_copy_sourcepath(e, "/src");

    archive_entry_free(e);
}

int main() {
    variant_pathname_utf8();
    variant_pathname_w();
    variant_copy_pathname();
    variant_size_is_set();
    variant_filetype_perm();
    variant_mtime();
    variant_atime();
    variant_ctime();
    variant_uid();
    variant_gid();
    variant_uname();
    variant_gname();
    variant_hardlink();
    variant_symlink();
    variant_set_link();
    variant_sourcepath();
    variant_copy_sourcepath();
    variant_copy_stat();
    variant_clone();
    variant_clear();
    variant_encryption_checks();
    variant_all_setters();
    return 0;
}
