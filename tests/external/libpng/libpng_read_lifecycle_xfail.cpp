// libpng read lifecycle — XFAIL: DTT deref postcondition limitation
//
// png_destroy_read_struct takes png_structpp (pointer-to-pointer) and uses
// "ensures arg0.deref.@png_state = 0" to reset state. DTT cannot currently
// resolve deref postconditions on free functions, so the scope_exit on
// png_create_read_struct (result.@png_state == 0) is never satisfied.
// This test documents the limitation — correct code that DTT flags.

#include <png.h>
#include <cstdio>
#include <cstdlib>

// Dummy read callback for custom I/O
void my_read_fn(png_structp, png_bytep, png_size_t) {}

// Variant 1: Full read lifecycle with init_io
void variant_full_read_with_init_io() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    FILE *fp = fopen("input.png", "rb");

    png_init_io(png_ptr, fp);               // requires @png_state == 1
    png_read_info(png_ptr, info_ptr);       // requires both @png_state == 1
    png_set_expand(png_ptr);                // requires @png_state == 1

    png_bytep row_pointers[1] = {nullptr};
    png_read_image(png_ptr, row_pointers);  // requires @png_state == 1
    png_read_end(png_ptr, info_ptr);        // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);  // @png_state = 0
    // scope_exit: @png_state == 0 — satisfied
}

// Variant 2: Read lifecycle with custom read function
void variant_read_with_custom_fn() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);  // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);    // requires @png_state == 1

    png_set_read_fn(png_ptr, nullptr, my_read_fn);  // requires @png_state == 1
    png_read_info(png_ptr, info_ptr);                // requires both @png_state == 1

    png_bytep row_pointers[1] = {nullptr};
    png_read_image(png_ptr, row_pointers);           // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);  // @png_state = 0
}

int main() {
    variant_full_read_with_init_io();
    variant_read_with_custom_fn();
    return 0;
}
