// libusb claim interface before open -- interface ops on closed handle (should NOT compile)
//
// Tests that DTT catches libusb_claim_interface when the device handle
// has not been opened. The @handle_open ghost field starts at 0 (default)
// and libusb_claim_interface requires @handle_open != 0.

#include <libusb.h>

// Variant 1: open then close, then claim on closed handle
void variant_claim_after_close(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);              // @handle_open = 1
    libusb_close(handle);                   // @handle_open = 0

    // DTT ERROR: requires @handle_open != 0, but @handle_open == 0
    libusb_claim_interface(handle, 0);
}

int main() {
    (void)variant_claim_after_close;
    return 0;
}
