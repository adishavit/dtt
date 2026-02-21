// libarchive write format and filter configuration -- correct usage (should COMPILE)
//
// Exercises ALL archive_write_set_format_* and archive_write_add_filter_*
// functions annotated in libarchive.dtt. Each variant creates a new write
// archive, configures a specific format or filter, opens, and then closes
// and frees inline (DTT tracks per-variable scope, no helper calls).

#include <archive.h>
#include <archive_entry.h>

// --- Write set_format variants ---

void variant_format_generic() {
    struct archive *a = archive_write_new();              // @archive_state = 1
    archive_write_set_format(a, 0x30000);                 // @archive_state = 2
    archive_write_open_filename(a, "out.tar");            // @archive_state = 3
    archive_write_close(a);                               // @archive_state = 5
    archive_write_free(a);                                // @archive_state = 0
}

void variant_format_by_name() {
    struct archive *a = archive_write_new();
    archive_write_set_format_by_name(a, "ustar");
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_7zip() {
    struct archive *a = archive_write_new();
    archive_write_set_format_7zip(a);
    archive_write_open_filename(a, "out.7z");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_ar_bsd() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ar_bsd(a);
    archive_write_open_filename(a, "out.a");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_ar_svr4() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ar_svr4(a);
    archive_write_open_filename(a, "out.a");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_cpio() {
    struct archive *a = archive_write_new();
    archive_write_set_format_cpio(a);
    archive_write_open_filename(a, "out.cpio");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_cpio_newc() {
    struct archive *a = archive_write_new();
    archive_write_set_format_cpio_newc(a);
    archive_write_open_filename(a, "out.cpio");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_iso9660() {
    struct archive *a = archive_write_new();
    archive_write_set_format_iso9660(a);
    archive_write_open_filename(a, "out.iso");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_pax() {
    struct archive *a = archive_write_new();
    archive_write_set_format_pax(a);
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_pax_restricted() {
    struct archive *a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_raw() {
    struct archive *a = archive_write_new();
    archive_write_set_format_raw(a);
    archive_write_open_filename(a, "out.raw");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_shar() {
    struct archive *a = archive_write_new();
    archive_write_set_format_shar(a);
    archive_write_open_filename(a, "out.shar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_shar_dump() {
    struct archive *a = archive_write_new();
    archive_write_set_format_shar_dump(a);
    archive_write_open_filename(a, "out.shar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_v7tar() {
    struct archive *a = archive_write_new();
    archive_write_set_format_v7tar(a);
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_xar() {
    struct archive *a = archive_write_new();
    archive_write_set_format_xar(a);
    archive_write_open_filename(a, "out.xar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_format_zip() {
    struct archive *a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_open_filename(a, "out.zip");
    archive_write_close(a);
    archive_write_free(a);
}

// --- Write add_filter variants ---

void variant_filter_generic() {
    struct archive *a = archive_write_new();
    archive_write_add_filter(a, 0);
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_by_name() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_by_name(a, "gzip");
    archive_write_open_filename(a, "out.tar.gz");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_bzip2() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_bzip2(a);
    archive_write_open_filename(a, "out.tar.bz2");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_compress() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_compress(a);
    archive_write_open_filename(a, "out.tar.Z");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_lz4() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_lz4(a);
    archive_write_open_filename(a, "out.tar.lz4");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_lzma() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_lzma(a);
    archive_write_open_filename(a, "out.tar.lzma");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_lzop() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_lzop(a);
    archive_write_open_filename(a, "out.tar.lzo");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_none() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_none(a);
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_xz() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_xz(a);
    archive_write_open_filename(a, "out.tar.xz");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_filter_zstd() {
    struct archive *a = archive_write_new();
    archive_write_add_filter_zstd(a);
    archive_write_open_filename(a, "out.tar.zst");
    archive_write_close(a);
    archive_write_free(a);
}

// --- Write open variants (alternative open functions) ---

void variant_open_filename_w() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_open_filename_w(a, L"out.tar");
    archive_write_close(a);
    archive_write_free(a);
}

void variant_open_memory() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    char buf[4096];
    size_t used = 0;
    archive_write_open_memory(a, buf, sizeof(buf), &used);
    archive_write_close(a);
    archive_write_free(a);
}

void variant_open_fd() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_open_fd(a, 1);
    archive_write_close(a);
    archive_write_free(a);
}

void variant_open_FILE() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    FILE *fp = (FILE*)0x1;
    archive_write_open_FILE(a, fp);
    archive_write_close(a);
    archive_write_free(a);
}

// --- Write data operations (with full inline lifecycle) ---

void variant_write_header_data() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_open_filename(a, "out.tar");

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "file.txt");
    archive_entry_set_size(entry, 5);
    archive_entry_set_filetype(entry, 0100000);
    archive_entry_set_perm(entry, 0644);

    archive_write_header(a, entry);
    const char data[] = "hello";
    archive_write_data(a, data, 5);
    archive_write_finish_entry(a);

    archive_entry_free(entry);
    archive_write_close(a);
    archive_write_free(a);
}

void variant_write_data_block() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_open_filename(a, "out.tar");

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "block.bin");
    archive_entry_set_size(entry, 8);
    archive_entry_set_filetype(entry, 0100000);
    archive_entry_set_perm(entry, 0644);

    archive_write_header(a, entry);
    const char block[] = "blockdat";
    archive_write_data_block(a, block, 8, 0);

    archive_entry_free(entry);
    archive_write_close(a);
    archive_write_free(a);
}

// --- Deprecated write_finish alias ---

void variant_write_finish() {
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_open_filename(a, "out.tar");
    archive_write_close(a);
    archive_write_finish(a);    // deprecated alias for archive_write_free
}

int main() {
    variant_format_generic();
    variant_format_by_name();
    variant_format_7zip();
    variant_format_ar_bsd();
    variant_format_ar_svr4();
    variant_format_cpio();
    variant_format_cpio_newc();
    variant_format_iso9660();
    variant_format_pax();
    variant_format_pax_restricted();
    variant_format_raw();
    variant_format_shar();
    variant_format_shar_dump();
    variant_format_v7tar();
    variant_format_xar();
    variant_format_zip();

    variant_filter_generic();
    variant_filter_by_name();
    variant_filter_bzip2();
    variant_filter_compress();
    variant_filter_lz4();
    variant_filter_lzma();
    variant_filter_lzop();
    variant_filter_none();
    variant_filter_xz();
    variant_filter_zstd();

    variant_open_filename_w();
    variant_open_memory();
    variant_open_fd();
    variant_open_FILE();

    variant_write_header_data();
    variant_write_data_block();
    variant_write_finish();
    return 0;
}
