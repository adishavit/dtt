// zlib gzip advanced I/O -- incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches @gz_open state violations for gzip file I/O
// functions that were not covered by the basic gz lifecycle fail tests.
// Each variant uses a gz function after gzclose.

#include <zlib.h>

// Variant 1: gzbuffer after gzclose
void variant_gzbuffer_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzbuffer(gz, 65536);  // dtt-error {{requires @gz_open}}
}

// Variant 2: gzfread after gzclose
void variant_gzfread_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    unsigned char buf[256];
    // DTT ERROR: requires arg3.@gz_open != 0 (required state is not established in enforce mode)
    gzfread(buf, 1, sizeof(buf), gz);  // dtt-error {{requires @gz_open}}
}

// Variant 3: gzfwrite after gzclose
void variant_gzfwrite_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    const char data[] = "Should not write";
    // DTT ERROR: requires arg3.@gz_open != 0 (required state is not established in enforce mode)
    gzfwrite(data, 1, sizeof(data), gz);  // dtt-error {{requires @gz_open}}
}

// Variant 4: gzprintf after gzclose
void variant_gzprintf_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: gzip stream has been closed.
    gzprintf(gz, "Should not print\n");  // dtt-error {{requires @gz_open}}
}

// Variant 5: gzputs after gzclose
void variant_gzputs_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzputs(gz, "Should not put");  // dtt-error {{requires @gz_open}}
}

// Variant 6: gzgets after gzclose
void variant_gzgets_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    char buf[256];
    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzgets(gz, buf, sizeof(buf));  // dtt-error {{requires @gz_open}}
}

// Variant 7: gzputc after gzclose
void variant_gzputc_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzputc(gz, 'X');  // dtt-error {{requires @gz_open}}
}

// Variant 8: gzgetc after gzclose
void variant_gzgetc_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzgetc(gz);  // dtt-error {{requires @gz_open}}
}

// Variant 9: gzungetc after gzclose
void variant_gzungetc_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg1.@gz_open != 0 (required state is not established in enforce mode)
    gzungetc('Y', gz);  // dtt-error {{requires @gz_open}}
}

// Variant 10: gzflush after gzclose
void variant_gzflush_after_close() {
    gzFile gz = gzopen("/tmp/dtt_test_fail.gz", "wb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzflush(gz, Z_SYNC_FLUSH);  // dtt-error {{requires @gz_open}}
}

// Variant 11: gzseek after gzclose
void variant_gzseek_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzseek(gz, 0, SEEK_SET);  // dtt-error {{requires @gz_open}}
}

// Variant 12: gzrewind after gzclose
void variant_gzrewind_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzrewind(gz);  // dtt-error {{requires @gz_open}}
}

// Variant 13: gztell after gzclose
void variant_gztell_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gztell(gz);  // dtt-error {{requires @gz_open}}
}

// Variant 14: gzoffset after gzclose
void variant_gzoffset_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzoffset(gz);  // dtt-error {{requires @gz_open}}
}

// Variant 15: gzeof after gzclose
void variant_gzeof_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzeof(gz);  // dtt-error {{requires @gz_open}}
}

// Variant 16: gzdirect after gzclose
void variant_gzdirect_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzdirect(gz);  // dtt-error {{requires @gz_open}}
}

// Variant 17: gzerror after gzclose
void variant_gzerror_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    int errnum = 0;
    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzerror(gz, &errnum);  // dtt-error {{requires @gz_open}}
}

// Variant 18: gzclearerr after gzclose
void variant_gzclearerr_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");    // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                              // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzclearerr(gz);  // dtt-error {{requires @gz_open}}
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
