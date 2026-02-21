// libpng info accessors — XFAIL: DTT deref postcondition limitation
//
// Exercises IHDR accessors, palette accessors, and image-property getters
// on a valid read struct + info struct pair. xfail because the scope_exit
// on png_create_read_struct cannot be satisfied via deref destroy.

#include <png.h>

// Variant 1: All IHDR getters
void variant_ihdr_getters() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_uint_32 width = 0, height = 0;
    int bit_depth = 0, color_type = 0;
    int interlace = 0, compress = 0, filter = 0;

    (void)png_get_IHDR(png_ptr, info_ptr,
                       &width, &height,
                       &bit_depth, &color_type,
                       &interlace, &compress, &filter);      // requires both @png_state == 1

    (void)png_get_image_width(png_ptr, info_ptr);            // requires both @png_state == 1
    (void)png_get_image_height(png_ptr, info_ptr);           // requires both @png_state == 1
    (void)png_get_bit_depth(png_ptr, info_ptr);              // requires both @png_state == 1
    (void)png_get_color_type(png_ptr, info_ptr);             // requires both @png_state == 1
    (void)png_get_rowbytes(png_ptr, info_ptr);               // requires both @png_state == 1

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);   // deref postcondition
}

// Variant 2: IHDR setter and palette accessors
void variant_ihdr_set_and_palette() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_set_IHDR(png_ptr, info_ptr,
                 320, 240, 8,
                 PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);                   // requires both @png_state == 1

    png_color palette[256];
    png_set_PLTE(png_ptr, info_ptr, palette, 256);           // requires both @png_state == 1

    png_colorp out_palette = nullptr;
    int num_palette = 0;
    (void)png_get_PLTE(png_ptr, info_ptr,
                       &out_palette, &num_palette);          // requires both @png_state == 1

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);   // deref postcondition
}

// Variant 3: Info struct lifecycle with destroy_info_struct
void variant_destroy_info_struct() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // @png_state = 1

    (void)png_get_image_width(png_ptr, info_ptr);            // requires both @png_state == 1

    png_destroy_info_struct(png_ptr, &info_ptr);             // info @png_state = 0
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);     // deref postcondition
}

int main() {
    variant_ihdr_getters();
    variant_ihdr_set_and_palette();
    variant_destroy_info_struct();
    return 0;
}
