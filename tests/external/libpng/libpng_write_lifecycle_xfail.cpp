// libpng write lifecycle — XFAIL: DTT deref postcondition limitation
//
// png_destroy_write_struct takes png_structpp (pointer-to-pointer) and uses
// "ensures arg0.deref.@png_state = 0" to reset state. DTT cannot currently
// resolve deref postconditions on free functions, so the scope_exit on
// png_create_write_struct (result.@png_state == 0) is never satisfied.
// This test documents the limitation — correct code that DTT flags.

#include <png.h>
#include <cstdio>

// Dummy write/flush callbacks for custom I/O
void my_write_fn(png_structp, png_bytep, png_size_t) {}
void my_flush_fn(png_structp) {}

// Variant 1: Full write lifecycle with init_io
void variant_full_write_with_init_io() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    FILE *fp = fopen("output.png", "wb");

    png_init_io(png_ptr, fp);                                // requires @png_state == 1
    png_set_IHDR(png_ptr, info_ptr,
                 640, 480, 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);                   // requires both @png_state == 1
    png_write_info(png_ptr, info_ptr);                       // requires both @png_state == 1

    png_bytep row_pointers[1] = {nullptr};
    png_write_image(png_ptr, row_pointers);                  // requires @png_state == 1
    png_write_end(png_ptr, info_ptr);                        // requires @png_state == 1

    png_destroy_write_struct(&png_ptr, &info_ptr);           // @png_state = 0
    // scope_exit: @png_state == 0 — satisfied
}

// Variant 2: Write lifecycle with custom write function (no image data)
void variant_write_with_custom_fn() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);     // requires @png_state == 1

    png_set_write_fn(png_ptr, nullptr,
                     my_write_fn, my_flush_fn);               // requires @png_state == 1
    png_set_IHDR(png_ptr, info_ptr,
                 320, 240, 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);                    // requires both @png_state == 1
    png_write_info(png_ptr, info_ptr);                        // requires both @png_state == 1
    png_write_end(png_ptr, info_ptr);                         // requires @png_state == 1

    png_destroy_write_struct(&png_ptr, &info_ptr);            // @png_state = 0
}

int main() {
    variant_full_write_with_init_io();
    variant_write_with_custom_fn();
    return 0;
}
