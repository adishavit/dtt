// zlib version/info functions -- correct usage patterns (should COMPILE)
//
// Tests the stateless version and info query functions: zlibVersion and
// zlibCompileFlags. These have no preconditions or postconditions in the
// DTT annotations.

#include <zlib.h>

// Variant 1: zlibVersion -- returns the library version string
void variant_zlib_version() {
    const char *ver = zlibVersion();
    (void)ver;
}

// Variant 2: zlibCompileFlags -- returns compile-time configuration flags
void variant_zlib_compile_flags() {
    uLong flags = zlibCompileFlags();
    (void)flags;
}

// Variant 3: both version functions used together
void variant_version_and_flags() {
    const char *ver = zlibVersion();
    uLong flags = zlibCompileFlags();
    (void)ver;
    (void)flags;
}

int main() {
    variant_zlib_version();
    variant_zlib_compile_flags();
    variant_version_and_flags();
    return 0;
}
