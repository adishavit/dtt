// zlib gzip advanced I/O -- incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches @gz_open state violations for gzip file I/O
// functions that were not covered by the basic gz lifecycle fail tests.
// Each variant uses a gz function after gzclose.

#include <zlib.h>

// Variant 1: gzbuffer after gzclose
void variant_gzbuffer_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzbuffer(gz, 65536);
}

// Variant 2: gzfread after gzclose
void variant_gzfread_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    unsigned char buf[256];
    // DTT ERROR: requires arg3.@gz_open != 0, but @gz_open == 0
    gzfread(buf, 1, sizeof(buf), gz);
}

// Variant 3: gzfwrite after gzclose
void variant_gzfwrite_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    const char data[] = "Should not write";
    // DTT ERROR: requires arg3.@gz_open != 0, but @gz_open == 0
    gzfwrite(data, 1, sizeof(data), gz);
}

// Variant 4: gzprintf after gzclose
void variant_gzprintf_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzprintf(gz, "Should not print %d\n", 42);
}

// Variant 5: gzputs after gzclose
void variant_gzputs_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzputs(gz, "Should not put");
}

// Variant 6: gzgets after gzclose
void variant_gzgets_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    char buf[256];
    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzgets(gz, buf, sizeof(buf));
}

// Variant 7: gzputc after gzclose
void variant_gzputc_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzputc(gz, 'X');
}

// Variant 8: gzgetc after gzclose
void variant_gzgetc_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzgetc(gz);
}

// Variant 9: gzungetc after gzclose
void variant_gzungetc_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg1.@gz_open != 0, but @gz_open == 0
    gzungetc('Y', gz);
}

// Variant 10: gzflush after gzclose
void variant_gzflush_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzflush(gz, Z_SYNC_FLUSH);
}

// Variant 11: gzseek after gzclose
void variant_gzseek_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzseek(gz, 0, SEEK_SET);
}

// Variant 12: gzrewind after gzclose
void variant_gzrewind_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzrewind(gz);
}

// Variant 13: gztell after gzclose
void variant_gztell_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gztell(gz);
}

// Variant 14: gzoffset after gzclose
void variant_gzoffset_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzoffset(gz);
}

// Variant 15: gzeof after gzclose
void variant_gzeof_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzeof(gz);
}

// Variant 16: gzdirect after gzclose
void variant_gzdirect_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzdirect(gz);
}

// Variant 17: gzerror after gzclose
void variant_gzerror_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    int errnum = 0;
    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzerror(gz, &errnum);
}

// Variant 18: gzclearerr after gzclose
void variant_gzclearerr_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0, but @gz_open == 0
    gzclearerr(gz);
}

int main() {
    variant_gzbuffer_after_close();
    variant_gzfread_after_close();
    variant_gzfwrite_after_close();
    variant_gzprintf_after_close();
    variant_gzputs_after_close();
    variant_gzgets_after_close();
    variant_gzputc_after_close();
    variant_gzgetc_after_close();
    variant_gzungetc_after_close();
    variant_gzflush_after_close();
    variant_gzseek_after_close();
    variant_gzrewind_after_close();
    variant_gztell_after_close();
    variant_gzoffset_after_close();
    variant_gzeof_after_close();
    variant_gzdirect_after_close();
    variant_gzerror_after_close();
    variant_gzclearerr_after_close();
    return 0;
}
