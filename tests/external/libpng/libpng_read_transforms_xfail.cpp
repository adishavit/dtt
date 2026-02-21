// libpng read transforms — XFAIL: DTT deref postcondition limitation
//
// Exercises all read transform functions on a valid png_struct. Because
// png_create_read_struct has scope_exit and png_destroy_read_struct uses
// deref postconditions (arg0.deref.@png_state = 0) which DTT cannot
// currently resolve, these are xfail despite being correct code.

#include <png.h>

// Variant 1: All single-arg transform functions on a read struct
void variant_all_read_transforms() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1

    png_set_expand(png_ptr);              // requires @png_state == 1
    png_set_palette_to_rgb(png_ptr);      // requires @png_state == 1
    png_set_gray_to_rgb(png_ptr);         // requires @png_state == 1
    png_set_strip_alpha(png_ptr);         // requires @png_state == 1
    png_set_strip_16(png_ptr);            // requires @png_state == 1
    png_set_bgr(png_ptr);                // requires @png_state == 1
    png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);  // requires @png_state == 1
    (void)png_set_interlace_handling(png_ptr);            // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);  // deref postcondition
}

// Variant 2: Transforms with extra arguments (quantize, gamma)
void variant_parameterized_transforms() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1

    png_color palette[256];
    png_set_quantize(png_ptr, palette, 256, 128, nullptr, 0);  // requires @png_state == 1
    png_set_gamma(png_ptr, 2.2, 0.45455);                      // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
}

int main() {
    variant_all_read_transforms();
    variant_parameterized_transforms();
    return 0;
}
