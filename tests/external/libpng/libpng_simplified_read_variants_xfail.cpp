// libpng simplified API read variants — XFAIL
//
// Exercises png_image_begin_read_from_memory and
// png_image_begin_read_from_stdio. These functions are annotated in
// the DTT file but may not match at compile time due to function
// signature resolution issues; DTT treats them as unannotated, which
// causes tracking to be lost and subsequent precondition checks to fail.

#include <png.h>
#include <cstdio>
#include <cstring>

// Variant 1: begin_read_from_memory -> finish_read -> free
void variant_read_from_memory() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char fake_png[64];
    memset(fake_png, 0, sizeof(fake_png));

    char buffer[1024];

    (void)png_image_begin_read_from_memory(
        &image, fake_png, sizeof(fake_png));                 // @png_state = 1
    (void)png_image_finish_read(
        &image, nullptr, buffer, 0, nullptr);                // requires @png_state == 1
    png_image_free(&image);                                  // @png_state = 0
}

// Variant 2: begin_read_from_stdio -> finish_read -> free
void variant_read_from_stdio() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    FILE *fp = fopen("input.png", "rb");
    char buffer[1024];

    (void)png_image_begin_read_from_stdio(&image, fp);       // @png_state = 1
    (void)png_image_finish_read(
        &image, nullptr, buffer, 0, nullptr);                // requires @png_state == 1
    png_image_free(&image);                                  // @png_state = 0
    fclose(fp);                                              // satisfy FILE* scope_exit
}

int main() {
    variant_read_from_memory();
    variant_read_from_stdio();
    return 0;
}
