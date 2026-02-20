// libusb null pointer safety -- null handle passed to API (should NOT compile)
//
// Tests that DTT catches null pointer arguments via _nonnull preconditions.
// libusb_close requires arg0._nonnull != 0, so passing a null handle
// must be flagged.

#include <libusb.h>
#include <stddef.h>

// Variant 1: close with null handle
void variant_close_null() {
    libusb_device_handle *handle = (libusb_device_handle*)0;

    // DTT ERROR: requires arg0._nonnull != 0, but handle is null
    libusb_close(handle);
}

int main() {
    variant_close_null();
    return 0;
}
