// zlib gzip file I/O lifecycle — incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches misuse of the gzip file handle lifecycle.
// Each variant triggers a precondition violation on @gz_open.

#include <zlib.h>

// Variant 1: gzread after gzclose — use after close
void variant_read_after_close() {
    gzFile gz = gzopen("/dev/null", "rb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                            // @gz_open = 0
    unsigned char buf[256];

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzread(gz, buf, sizeof(buf));  // dtt-error {{requires @gz_open}}
}

// Variant 2: gzclose called twice — double close
void variant_double_gzclose() {
    gzFile gz = gzopen("/dev/null", "rb");  // @gz_open = 1
    if (!gz) return;
    gzclose(gz);                            // @gz_open = 0

    // DTT ERROR: requires arg0.@gz_open != 0 (required state is not established in enforce mode)
    gzclose(gz);  // dtt-error {{requires @gz_open}}
}

int main() {
    variant_read_after_close();
    variant_double_gzclose();
    return 0;
}
