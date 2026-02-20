// libpng misc utilities — XFAIL: DTT deref postcondition limitation
//
// Exercises utility functions: png_get_io_ptr, png_set_sig_bytes,
// png_sig_cmp, png_access_version_number. Functions that operate on
// a png_struct are xfail due to the deref destroy limitation.
// png_sig_cmp and png_access_version_number have no state requirements
// but are grouped here for coverage.

#include <png.h>
#include <cstdio>

void my_read_cb(png_structp, png_bytep, png_size_t) {}

// Variant 1: I/O pointer and signature utilities
void variant_io_ptr_and_sig() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1

    FILE *fp = fopen("input.png", "rb");
    png_init_io(png_ptr, fp);                                // requires @png_state == 1

    (void)png_get_io_ptr(png_ptr);                           // requires @png_state == 1
    png_set_sig_bytes(png_ptr, 8);                           // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);     // deref postcondition
}

// Variant 2: Signature comparison (stateless, no struct needed)
void variant_sig_cmp() {
    png_byte header[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    int result = png_sig_cmp(header, 0, 8);                  // requires arg0._nonnull
    (void)result;
}

// Variant 3: Version number query (no arguments, no state)
void variant_version_number() {
    png_uint_32 ver = png_access_version_number();           // no preconditions
    (void)ver;
}

int main() {
    variant_io_ptr_and_sig();
    variant_sig_cmp();
    variant_version_number();
    return 0;
}
