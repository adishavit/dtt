// CVE-2015-8126: Buffer overflow in png_set_PLTE (should FAIL to compile)
//
// CVE-2015-8126 involves a buffer overflow in png_set_PLTE and png_get_PLTE
// when called with an invalid palette size. A common exploit pattern uses a
// destroyed or improperly initialized png struct. DTT catches this because
// png_set_PLTE requires both arg0.@png_state == 1 and arg1.@png_state == 1;
// calling it on a destroyed struct violates this precondition.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2015-8126

#include <png.h>

// Variant 1: png_set_PLTE on destroyed read struct
void variant_set_plte_after_read_destroy() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);   // @png_state = 0

    png_color palette[256];

    // DTT ERROR: requires arg0.@png_state == 1, but @png_state == 0
    png_set_PLTE(png_ptr, info_ptr, palette, 256);
}

// Variant 2: png_get_PLTE on destroyed write struct
void variant_get_plte_after_write_destroy() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_destroy_write_struct(&png_ptr, &info_ptr);           // @png_state = 0

    png_colorp palette = nullptr;
    int num_palette = 0;

    // DTT ERROR: requires arg0.@png_state == 1, but @png_state == 0
    (void)png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
}

int main() {
    variant_set_plte_after_read_destroy();
    variant_get_plte_after_write_destroy();
    return 0;
}
