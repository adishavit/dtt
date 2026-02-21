// libarchive read format and filter configuration -- correct usage (should COMPILE)
//
// Exercises ALL archive_read_support_format_* and archive_read_support_filter_*
// functions annotated in libarchive.dtt. Each variant creates a new archive,
// configures a specific format or filter, opens, performs a minimal read
// operation, then closes and frees the archive.

#include <archive.h>
#include <archive_entry.h>

// --- Read support filter variants ---

// Each filter function transitions @archive_state from 1 to 2.
// Because DTT sees the first config call as the 1->2 transition, each
// variant uses exactly one filter call before opening.

void variant_filter_bzip2() {
    struct archive *a = archive_read_new();             // @archive_state = 1
    archive_read_support_filter_bzip2(a);               // @archive_state = 2
    archive_read_open_filename(a, "t.tar.bz2", 10240); // @archive_state = 3
    archive_read_close(a);                              // @archive_state = 5
    archive_read_free(a);                               // @archive_state = 0
}

void variant_filter_compress() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_compress(a);
    archive_read_open_filename(a, "t.tar.Z", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_gzip() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_gzip(a);
    archive_read_open_filename(a, "t.tar.gz", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_lz4() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_lz4(a);
    archive_read_open_filename(a, "t.tar.lz4", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_lzma() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_lzma(a);
    archive_read_open_filename(a, "t.tar.lzma", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_none() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_none(a);
    archive_read_open_filename(a, "t.tar", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_xz() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_xz(a);
    archive_read_open_filename(a, "t.tar.xz", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_filter_zstd() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_zstd(a);
    archive_read_open_filename(a, "t.tar.zst", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

// --- Read support format variants ---

void variant_format_7zip() {
    struct archive *a = archive_read_new();
    archive_read_support_format_7zip(a);
    archive_read_open_filename(a, "t.7z", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_ar() {
    struct archive *a = archive_read_new();
    archive_read_support_format_ar(a);
    archive_read_open_filename(a, "t.a", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_cab() {
    struct archive *a = archive_read_new();
    archive_read_support_format_cab(a);
    archive_read_open_filename(a, "t.cab", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_cpio() {
    struct archive *a = archive_read_new();
    archive_read_support_format_cpio(a);
    archive_read_open_filename(a, "t.cpio", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_empty() {
    struct archive *a = archive_read_new();
    archive_read_support_format_empty(a);
    archive_read_open_filename(a, "t.empty", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_iso9660() {
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_open_filename(a, "t.iso", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_lha() {
    struct archive *a = archive_read_new();
    archive_read_support_format_lha(a);
    archive_read_open_filename(a, "t.lha", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_mtree() {
    struct archive *a = archive_read_new();
    archive_read_support_format_mtree(a);
    archive_read_open_filename(a, "t.mtree", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_rar() {
    struct archive *a = archive_read_new();
    archive_read_support_format_rar(a);
    archive_read_open_filename(a, "t.rar", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_rar5() {
    struct archive *a = archive_read_new();
    archive_read_support_format_rar5(a);
    archive_read_open_filename(a, "t.rar5", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_raw() {
    struct archive *a = archive_read_new();
    archive_read_support_format_raw(a);
    archive_read_open_filename(a, "t.raw", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_xar() {
    struct archive *a = archive_read_new();
    archive_read_support_format_xar(a);
    archive_read_open_filename(a, "t.xar", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_format_zip() {
    struct archive *a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_open_filename(a, "t.zip", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

// --- Read open variants (alternative open functions) ---

void variant_open_filename_w() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename_w(a, L"t.tar", 10240);  // wide-char filename
    archive_read_close(a);
    archive_read_free(a);
}

void variant_open_memory() {
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    char buf[64] = {0};
    archive_read_open_memory(a, buf, sizeof(buf));
    archive_read_close(a);
    archive_read_free(a);
}

void variant_open_fd() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_fd(a, 0, 10240);  // fd 0 = stdin, block_size
    archive_read_close(a);
    archive_read_free(a);
}

// --- Read data variants ---

void variant_read_data_block() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "t.tar", 10240);

    struct archive_entry *entry = nullptr;
    archive_read_next_header(a, &entry);

    const void *block = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;
    archive_read_data_block(a, &block, &size, &offset);

    archive_read_close(a);
    archive_read_free(a);
}

void variant_read_data_into_fd() {
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_open_filename(a, "t.tar", 10240);

    struct archive_entry *entry = nullptr;
    archive_read_next_header(a, &entry);
    archive_read_data_into_fd(a, 1); // fd 1 = stdout

    archive_read_close(a);
    archive_read_free(a);
}

void variant_read_next_header2() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "t.tar", 10240);

    struct archive_entry *entry = archive_entry_new();
    archive_read_next_header2(a, entry);
    archive_entry_free(entry);

    archive_read_close(a);
    archive_read_free(a);
}

// --- Read extract variants ---

void variant_read_extract() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "t.tar", 10240);

    struct archive_entry *entry = (struct archive_entry*)0x1;
    archive_read_next_header(a, &entry);
    archive_read_extract(a, entry, 0);

    archive_read_close(a);
    archive_read_free(a);
}

void variant_read_extract2() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "t.tar", 10240);

    struct archive *ext = archive_write_disk_new();

    struct archive_entry *entry = (struct archive_entry*)0x1;
    archive_read_next_header(a, &entry);
    archive_read_extract2(a, entry, ext);

    archive_write_free(ext);
    archive_read_close(a);
    archive_read_free(a);
}

void variant_read_extract_set_progress_callback() {
    struct archive *a = archive_read_new();
    archive_read_extract_set_progress_callback(a, 0, 0);
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "t.tar", 10240);
    archive_read_close(a);
    archive_read_free(a);
}

// --- Deprecated alias ---

void variant_read_finish() {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_open_filename(a, "t.tar", 10240);
    archive_read_close(a);
    archive_read_finish(a);   // deprecated alias for archive_read_free
}

// --- Callback-based open ---

void variant_callbacks() {
    struct archive *a = archive_read_new();
    archive_read_set_read_callback(a, 0);
    archive_read_set_callback_data(a, 0);
    archive_read_set_close_callback(a, 0);
    archive_read_set_skip_callback(a, 0);
    archive_read_set_seek_callback(a, 0);
    archive_read_set_open_callback(a, 0);
    archive_read_set_switch_callback(a, 0);
    archive_read_append_callback_data(a, 0);
    archive_read_prepend_callback_data(a, 0);
    archive_read_support_filter_all(a);
    archive_read_open1(a);     // callback-based open
    archive_read_close(a);
    archive_read_free(a);
}

// --- Read open FILE ---

void variant_open_FILE() {
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    FILE *fp = (FILE*)0x1; // non-null placeholder for DTT analysis
    archive_read_open_FILE(a, fp);
    archive_read_close(a);
    archive_read_free(a);
}

int main() {
    variant_filter_bzip2();
    variant_filter_compress();
    variant_filter_gzip();
    variant_filter_lz4();
    variant_filter_lzma();
    variant_filter_none();
    variant_filter_xz();
    variant_filter_zstd();

    variant_format_7zip();
    variant_format_ar();
    variant_format_cab();
    variant_format_cpio();
    variant_format_empty();
    variant_format_iso9660();
    variant_format_lha();
    variant_format_mtree();
    variant_format_rar();
    variant_format_rar5();
    variant_format_raw();
    variant_format_xar();
    variant_format_zip();

    variant_open_filename_w();
    variant_open_memory();
    variant_open_fd();
    variant_open_FILE();

    variant_read_data_block();
    variant_read_data_into_fd();
    variant_read_next_header2();

    variant_read_extract();
    variant_read_extract2();
    variant_read_extract_set_progress_callback();

    variant_read_finish();
    variant_callbacks();
    return 0;
}
