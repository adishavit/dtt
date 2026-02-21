// libpng use after destroy — both read and write paths (should FAIL to compile)
//
// Tests that DTT catches operations on a png struct after it has been
// destroyed. This applies to both read and write lifecycles: after
// png_destroy_*_struct sets @png_state = 0, any function requiring
// @png_state == 1 must be rejected.

#include <png.h>

// Variant 1: create_info after destroy_read_struct
void variant_create_info_after_read_destroy() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);     // @png_state = 0

    // DTT ERROR: requires arg0.@png_state == 1, but @png_state == 0
    png_infop info_ptr = png_create_info_struct(png_ptr);
    (void)info_ptr;
}

// Variant 2: write_info after destroy_write_struct
void variant_write_info_after_write_destroy() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_destroy_write_struct(&png_ptr, &info_ptr);           // @png_state = 0

    // DTT ERROR: requires arg0.@png_state == 1, but @png_state == 0
    png_write_info(png_ptr, info_ptr);
}

int main() {
    variant_create_info_after_read_destroy();
    variant_write_info_after_write_destroy();
    return 0;
}
