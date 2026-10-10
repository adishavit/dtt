// Assertions check the declared property at each misuse site in enforce mode.
// KNOWN GAP: these diagnostics currently report an unproved state, not a
// proven numeric violation. Setup/postcondition tracking is also incomplete.
// libpng read lifecycle — incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches read operations after the read struct has been
// destroyed. Each variant triggers a precondition violation because
// @png_state == 0 after png_destroy_read_struct, but the read functions
// require @png_state == 1.

#include <png.h>
#include <cstdio>

// Variant 1: png_read_info after destroy
void variant_read_info_after_destroy() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    if (!png_ptr) return;
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1
    if (!info_ptr) { png_destroy_read_struct(&png_ptr, nullptr, nullptr); return; }

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);   // @png_state = 0

    // Requires @png_state == 1; this call must be rejected in enforce mode.
    png_read_info(png_ptr, info_ptr); // dtt-error {{@png_state == 1.*png_ptr}} // dtt-error {{@png_state == 1.*info_ptr}}
}

// Variant 2: png_read_image after destroy
void variant_read_image_after_destroy() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    if (!png_ptr) return;

    png_bytep row_pointers[1] = {nullptr};

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);     // @png_state = 0

    // Requires @png_state == 1; this call must be rejected in enforce mode.
    png_read_image(png_ptr, row_pointers); // dtt-error {{@png_state == 1}}
}

int main() {
    variant_read_info_after_destroy();
    variant_read_image_after_destroy();
    return 0;
}
