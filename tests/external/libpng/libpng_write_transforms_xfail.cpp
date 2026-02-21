// libpng write transforms and config — XFAIL: DTT deref postcondition limitation
//
// Exercises write-side transform and configuration functions. Because
// png_create_write_struct has scope_exit and png_destroy_write_struct uses
// deref postconditions that DTT cannot currently resolve, these are xfail.

#include <png.h>
#include <cstdio>

void my_write_cb(png_structp, png_bytep, png_size_t) {}
void my_flush_cb(png_structp) {}

// Variant 1: Write transforms applied to a write struct
void variant_write_transforms() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1

    png_set_bgr(png_ptr);                                   // requires @png_state == 1
    png_set_strip_alpha(png_ptr);                            // requires @png_state == 1
    png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_BEFORE);    // requires @png_state == 1

    png_destroy_write_struct(&png_ptr, nullptr);             // deref postcondition
}

// Variant 2: Full write pipeline with row-level I/O
void variant_write_row_io() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_set_write_fn(png_ptr, nullptr,
                     my_write_cb, my_flush_cb);              // requires @png_state == 1
    png_set_IHDR(png_ptr, info_ptr,
                 64, 64, 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);                   // requires both @png_state == 1
    png_write_info(png_ptr, info_ptr);                       // requires both @png_state == 1

    png_byte row[64 * 3];
    png_write_row(png_ptr, row);                             // requires @png_state == 1

    png_bytep rows[1] = { row };
    png_write_rows(png_ptr, rows, 1);                        // requires @png_state == 1

    png_write_end(png_ptr, info_ptr);                        // requires @png_state == 1

    png_destroy_write_struct(&png_ptr, &info_ptr);           // deref postcondition
}

int main() {
    variant_write_transforms();
    variant_write_row_io();
    return 0;
}
