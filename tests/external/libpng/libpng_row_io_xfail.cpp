// libpng row-level I/O — XFAIL: DTT deref postcondition limitation
//
// Exercises png_read_row, png_read_rows, png_write_row, png_write_rows,
// and png_read_update_info. xfail because the struct lifecycle uses
// deref destroy postconditions that DTT cannot resolve.

#include <png.h>
#include <cstdio>

void my_read_cb(png_structp, png_bytep, png_size_t) {}
void my_write_cb(png_structp, png_bytep, png_size_t) {}
void my_flush_cb(png_structp) {}

// Variant 1: Row-level read with png_read_row
void variant_read_row() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);     // requires @png_state == 1

    png_set_read_fn(png_ptr, nullptr, my_read_cb);           // requires @png_state == 1
    png_read_info(png_ptr, info_ptr);                        // requires both @png_state == 1
    png_read_update_info(png_ptr, info_ptr);                 // requires both @png_state == 1

    png_byte row[256];
    png_read_row(png_ptr, row, nullptr);                     // requires @png_state == 1

    png_read_end(png_ptr, info_ptr);                         // requires @png_state == 1
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);   // deref postcondition
}

// Variant 2: Batch read with png_read_rows
void variant_read_rows() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);     // requires @png_state == 1

    png_set_read_fn(png_ptr, nullptr, my_read_cb);           // requires @png_state == 1
    png_read_info(png_ptr, info_ptr);                        // requires both @png_state == 1

    png_byte row_buf[256];
    png_bytep rows[1] = { row_buf };
    png_read_rows(png_ptr, rows, nullptr, 1);                // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);   // deref postcondition
}

// Variant 3: Row-level write with png_write_row
void variant_write_row() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);     // requires @png_state == 1

    png_set_write_fn(png_ptr, nullptr,
                     my_write_cb, my_flush_cb);              // requires @png_state == 1
    png_set_IHDR(png_ptr, info_ptr,
                 32, 32, 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);                   // requires both @png_state == 1
    png_write_info(png_ptr, info_ptr);                       // requires both @png_state == 1

    png_byte row[32 * 3];
    png_write_row(png_ptr, row);                             // requires @png_state == 1

    png_write_end(png_ptr, info_ptr);                        // requires @png_state == 1
    png_destroy_write_struct(&png_ptr, &info_ptr);           // deref postcondition
}

// Variant 4: Batch write with png_write_rows
void variant_write_rows() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1
    png_infop info_ptr = png_create_info_struct(png_ptr);     // requires @png_state == 1

    png_set_write_fn(png_ptr, nullptr,
                     my_write_cb, my_flush_cb);              // requires @png_state == 1
    png_set_IHDR(png_ptr, info_ptr,
                 32, 32, 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);                   // requires both @png_state == 1
    png_write_info(png_ptr, info_ptr);                       // requires both @png_state == 1

    png_byte row[32 * 4];
    png_bytep rows[1] = { row };
    png_write_rows(png_ptr, rows, 1);                        // requires @png_state == 1

    png_write_end(png_ptr, info_ptr);                        // requires @png_state == 1
    png_destroy_write_struct(&png_ptr, &info_ptr);           // deref postcondition
}

int main() {
    variant_read_row();
    variant_read_rows();
    variant_write_row();
    variant_write_rows();
    return 0;
}
