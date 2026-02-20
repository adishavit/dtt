// libpng simplified API — correct usage patterns (should COMPILE)
//
// Tests that the simplified API (png_image_*) lifecycle is tracked
// correctly by DTT ghost field @png_state. The simplified API uses
// png_image structs instead of png_struct/png_info pairs.

#include <png.h>
#include <cstring>

// Variant 1: begin_read_from_file -> finish_read -> image_free
void variant_read_and_free() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char buffer[1024];

    (void)png_image_begin_read_from_file(&image, "input.png");  // @png_state = 1
    (void)png_image_finish_read(&image, nullptr,
                                buffer, 0, nullptr);            // requires @png_state == 1
    png_image_free(&image);                                     // @png_state = 0
}

// Variant 2: begin_read_from_file -> finish_read -> write_to_file -> image_free
void variant_read_then_write() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char buffer[1024];

    (void)png_image_begin_read_from_file(&image, "input.png");  // @png_state = 1
    (void)png_image_finish_read(&image, nullptr,
                                buffer, 0, nullptr);            // requires @png_state == 1
    (void)png_image_write_to_file(&image, "output.png", 0,
                                  buffer, 0, nullptr);          // requires @png_state == 1
    png_image_free(&image);                                     // @png_state = 0
}

int main() {
    variant_read_and_free();
    variant_read_then_write();
    return 0;
}
