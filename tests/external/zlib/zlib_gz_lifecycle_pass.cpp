// zlib gzip file I/O lifecycle — correct usage patterns (should COMPILE)
//
// Tests that gzopen -> gzread/gzwrite -> gzclose lifecycle is tracked
// correctly by DTT ghost field @gz_open. Each variant properly closes
// the file handle before scope exit to satisfy scope_exit.

#include <zlib.h>

// Variant 1: gzip read lifecycle
void variant_gz_read() {
    gzFile gz = gzopen("/dev/null", "rb");  // @gz_open = 1
    unsigned char buf[256];
    gzread(gz, buf, sizeof(buf));           // requires @gz_open != 0
    gzclose(gz);                            // @gz_open = 0
    // scope_exit: @gz_open == 0 — satisfied
}

// Variant 2: gzip write lifecycle
void variant_gz_write() {
    gzFile gz = gzopen("/tmp/test.gz", "wb");  // @gz_open = 1
    const char data[] = "Hello, gzip!";
    gzwrite(gz, data, sizeof(data));            // requires @gz_open != 0
    gzclose(gz);                                // @gz_open = 0
}

int main() {
    variant_gz_read();
    variant_gz_write();
    return 0;
}
