// libpng simplified API — additional write variants (should COMPILE)
//
// Tests simplified API write functions that use direct arg0 annotations
// (not deref): write_to_file (already covered), write_to_stdio,
// write_to_memory. These should pass because png_image_free uses
// "ensures arg0.@png_state = 0" (direct, not deref).
//
// NOTE: fopen creates a scope_exit tracked by DTT, so all FILE* must
// be properly fclose'd.

#include <png.h>
#include <cstdio>
#include <cstring>

// Variant 1: begin_read -> finish_read -> write_to_stdio -> free (with fclose)
void variant_write_to_stdio() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char buffer[1024];

    (void)png_image_begin_read_from_file(&image, "input.png");  // @png_state = 1
    (void)png_image_finish_read(
        &image, nullptr, buffer, 0, nullptr);                   // requires @png_state == 1

    FILE *out = fopen("output.png", "wb");
    (void)png_image_write_to_stdio(
        &image, out, 0, buffer, 0, nullptr);                    // requires @png_state == 1
    fclose(out);                                                // satisfy FILE* scope_exit
    png_image_free(&image);                                     // @png_state = 0
}

// Variant 2: begin_read -> finish_read -> write_to_memory -> free
void variant_write_to_memory() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char buffer[1024];

    (void)png_image_begin_read_from_file(&image, "input.png");  // @png_state = 1
    (void)png_image_finish_read(
        &image, nullptr, buffer, 0, nullptr);                   // requires @png_state == 1

    char out_buf[4096];
    png_alloc_size_t out_size = sizeof(out_buf);
    (void)png_image_write_to_memory(
        &image, out_buf, &out_size, 0, buffer, 0, nullptr);    // requires @png_state == 1
    png_image_free(&image);                                     // @png_state = 0
}

int main() {
    variant_write_to_stdio();
    variant_write_to_memory();
    return 0;
}
