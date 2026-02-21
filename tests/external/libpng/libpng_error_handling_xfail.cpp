// libpng error handling — XFAIL: DTT deref postcondition limitation
//
// Exercises png_set_error_fn, png_error, and png_warning. xfail because
// the struct is created via png_create_read_struct which has a scope_exit
// that cannot be satisfied through the deref destroy pattern.

#include <png.h>

void my_error_fn(png_structp, png_const_charp) {}
void my_warning_fn(png_structp, png_const_charp) {}

// Variant 1: Custom error handler setup
void variant_set_error_fn() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1

    png_set_error_fn(png_ptr, nullptr,
                     my_error_fn, my_warning_fn);            // requires @png_state == 1

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);     // deref postcondition
}

// Variant 2: png_warning on a valid struct
void variant_warning_call() {
    png_structp png_ptr = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1

    png_warning(png_ptr, "test warning message");            // requires arg0._nonnull

    png_destroy_read_struct(&png_ptr, nullptr, nullptr);     // deref postcondition
}

// Variant 3: png_error on a valid struct (noreturn in practice, but DTT checks preconditions)
void variant_error_call() {
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);   // @png_state = 1

    png_error(png_ptr, "test error message");                // requires arg0._nonnull

    png_destroy_write_struct(&png_ptr, nullptr);             // deref postcondition
}

int main() {
    variant_set_error_fn();
    variant_warning_call();
    variant_error_call();
    return 0;
}
