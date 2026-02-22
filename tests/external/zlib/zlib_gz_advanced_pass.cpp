// zlib gzip advanced I/O -- correct usage patterns (should COMPILE)
//
// Tests gzip file I/O functions not covered by the basic gz lifecycle tests:
// gzdopen, gzbuffer, gzfread, gzfwrite, gzprintf, gzputs, gzgets,
// gzputc, gzgetc, gzungetc, gzflush, gzseek, gzrewind, gztell, gzoffset,
// gzeof, gzdirect, gzclose_r, gzclose_w, gzerror, gzclearerr.
// All require @gz_open != 0 on the gzFile handle.
//
// Each variant null-checks the gzFile after gzopen/gzdopen so that the
// deferred `when` guard resolves _nonnull and @gz_open inside the branch.

#include <zlib.h>

// Variant 1: gzdopen -- attach gzip stream to an existing file descriptor
// Uses a literal fd value since DTT runs -fsyntax-only (no runtime execution).
// Avoids calling open() which would trigger DTT's libc @fd scope_exit tracking.
void variant_gzdopen(int fd) {
    gzFile gz = gzdopen(fd, "wb");
    if (gz) {
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 2: gzbuffer -- set internal buffer size after open
void variant_gzbuffer() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzbuffer(gz, 65536);                      // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 3: gzfread -- fread-style block read from gzip stream
void variant_gzfread() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        unsigned char buf[256];
        gzfread(buf, 1, sizeof(buf), gz);         // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 4: gzfwrite -- fwrite-style block write to gzip stream
void variant_gzfwrite() {
    gzFile gz = gzopen("/tmp/dtt_test_gzfwrite.gz", "wb");
    if (gz) {
        const char data[] = "Block write via gzfwrite";
        gzfwrite(data, 1, sizeof(data), gz);      // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 5: gzprintf -- formatted write to gzip stream
void variant_gzprintf() {
    gzFile gz = gzopen("/tmp/dtt_test_gzprintf.gz", "wb");
    if (gz) {
        gzprintf(gz, "Value: %d, Name: %s\n", 42, "test");     // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 6: gzputs -- write string to gzip stream
void variant_gzputs() {
    gzFile gz = gzopen("/tmp/dtt_test_gzputs.gz", "wb");
    if (gz) {
        gzputs(gz, "String written via gzputs\n");              // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 7: gzgets -- read line from gzip stream
void variant_gzgets() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        char buf[256];
        gzgets(gz, buf, sizeof(buf));             // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 8: gzputc -- write single character to gzip stream
void variant_gzputc() {
    gzFile gz = gzopen("/tmp/dtt_test_gzputc.gz", "wb");
    if (gz) {
        gzputc(gz, 'A');                          // requires @gz_open != 0
        gzputc(gz, 'B');
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 9: gzgetc -- read single character from gzip stream
void variant_gzgetc() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        int ch = gzgetc(gz);                      // requires @gz_open != 0
        (void)ch;
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 10: gzungetc -- push back a character into the gzip read stream
void variant_gzungetc() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzungetc('X', gz);                        // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 11: gzflush -- flush pending output to gzip stream
void variant_gzflush() {
    gzFile gz = gzopen("/tmp/dtt_test_gzflush.gz", "wb");
    if (gz) {
        const char data[] = "Flush test data";
        gzwrite(gz, data, sizeof(data));
        gzflush(gz, Z_SYNC_FLUSH);               // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 12: gzseek -- seek within gzip stream
void variant_gzseek() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzseek(gz, 0, SEEK_SET);                  // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 13: gzrewind -- rewind gzip stream to beginning
void variant_gzrewind() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzrewind(gz);                             // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 14: gztell -- get current position in gzip stream
void variant_gztell() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        z_off_t pos = gztell(gz);                 // requires @gz_open != 0
        (void)pos;
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 15: gzoffset -- get compressed offset in gzip stream
void variant_gzoffset() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        z_off_t off = gzoffset(gz);               // requires @gz_open != 0
        (void)off;
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 16: gzeof -- check end-of-file on gzip stream
void variant_gzeof() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        int eof = gzeof(gz);                      // requires @gz_open != 0
        (void)eof;
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 17: gzdirect -- check if gzip stream is reading uncompressed data
void variant_gzdirect() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        int direct = gzdirect(gz);                // requires @gz_open != 0
        (void)direct;
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 18: gzclose_r -- close read-mode gzip stream
void variant_gzclose_r() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzclose_r(gz);                            // @gz_open = 0
    }
}

// Variant 19: gzclose_w -- close write-mode gzip stream
void variant_gzclose_w() {
    gzFile gz = gzopen("/tmp/dtt_test_gzclose_w.gz", "wb");
    if (gz) {
        gzclose_w(gz);                            // @gz_open = 0
    }
}

// Variant 20: gzerror -- get error string for gzip stream
void variant_gzerror() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        int errnum = 0;
        const char *msg = gzerror(gz, &errnum);   // requires @gz_open != 0
        (void)msg;
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 21: gzclearerr -- clear error and EOF flags on gzip stream
void variant_gzclearerr() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzclearerr(gz);                           // requires @gz_open != 0
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 22: combined read workflow -- open, buffer, read, seek, tell, rewind, eof, close
void variant_gz_read_workflow() {
    gzFile gz = gzopen("/dev/null", "rb");
    if (gz) {
        gzbuffer(gz, 32768);
        unsigned char buf[128];
        gzread(gz, buf, sizeof(buf));
        z_off_t pos = gztell(gz);
        (void)pos;
        gzrewind(gz);
        int eof = gzeof(gz);
        (void)eof;
        int direct = gzdirect(gz);
        (void)direct;
        z_off_t off = gzoffset(gz);
        (void)off;
        int errnum = 0;
        const char *msg = gzerror(gz, &errnum);
        (void)msg;
        gzclearerr(gz);
        gzclose(gz);                              // @gz_open = 0
    }
}

// Variant 23: combined write workflow -- open, printf, puts, putc, flush, close
void variant_gz_write_workflow() {
    gzFile gz = gzopen("/tmp/dtt_test_workflow.gz", "wb");
    if (gz) {
        gzprintf(gz, "Line %d: %s\n", 1, "formatted");
        gzputs(gz, "Line 2: plain string\n");
        gzputc(gz, '\n');
        gzflush(gz, Z_SYNC_FLUSH);
        const char block[] = "Block write data";
        gzfwrite(block, 1, sizeof(block), gz);
        gzclose(gz);                              // @gz_open = 0
    }
}

int main() {
    variant_gzdopen(1);
    variant_gzbuffer();
    variant_gzfread();
    variant_gzfwrite();
    variant_gzprintf();
    variant_gzputs();
    variant_gzgets();
    variant_gzputc();
    variant_gzgetc();
    variant_gzungetc();
    variant_gzflush();
    variant_gzseek();
    variant_gzrewind();
    variant_gztell();
    variant_gzoffset();
    variant_gzeof();
    variant_gzdirect();
    variant_gzclose_r();
    variant_gzclose_w();
    variant_gzerror();
    variant_gzclearerr();
    variant_gz_read_workflow();
    variant_gz_write_workflow();
    return 0;
}
