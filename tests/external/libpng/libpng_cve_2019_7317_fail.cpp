// CVE-2019-7317: Use-after-free in png_image_free (should FAIL to compile)
//
// CVE-2019-7317 is a use-after-free vulnerability where the png_image struct
// is accessed after png_image_free has released its internal resources. In the
// original bug, a longjmp during png_image_free left the image in an
// inconsistent state, and subsequent operations triggered use-after-free.
// DTT catches this because png_image_free sets @png_state = 0, and all
// image operations require @png_state == 1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2019-7317

#include <png.h>
#include <cstring>

// Variant 1: png_image_finish_read after png_image_free
void variant_finish_read_after_free() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char buffer[1024];

    (void)png_image_begin_read_from_file(&image, "input.png");  // @png_state = 1
    png_image_free(&image);                                     // @png_state = 0

    // DTT ERROR: requires arg0.@png_state == 1, but @png_state == 0
    (void)png_image_finish_read(&image, nullptr, buffer, 0, nullptr);
}

// Variant 2: png_image_write_to_file after png_image_free
void variant_write_after_free() {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    char buffer[1024];

    (void)png_image_begin_read_from_file(&image, "input.png");  // @png_state = 1
    (void)png_image_finish_read(&image, nullptr,
                                buffer, 0, nullptr);            // requires @png_state == 1
    png_image_free(&image);                                     // @png_state = 0

    // DTT ERROR: requires arg0.@png_state == 1, but @png_state == 0
    (void)png_image_write_to_file(&image, "output.png", 0, buffer, 0, nullptr);
}

int main() {
    variant_finish_read_after_free();
    variant_write_after_free();
    return 0;
}
