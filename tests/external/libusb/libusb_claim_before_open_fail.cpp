// libusb claim interface before open -- interface ops on closed handle (should NOT compile)
//
// Tests that DTT catches libusb_claim_interface when the device handle
// has been closed. The declared libusb_close contract sets @handle_open to 0;
// libusb_claim_interface requires @handle_open != 0.

#include <libusb.h>

// Variant 1: open then close, then claim on closed handle
void variant_claim_after_close(libusb_device *dev) {
    if (!dev) return;
    libusb_device_handle *handle = nullptr;
    libusb_open(dev, &handle);              // @handle_open = 1
    if (!handle) return;
    libusb_close(handle);                   // @handle_open = 0

    // Requires @handle_open != 0; this call must be rejected in enforce mode.
    libusb_claim_interface(handle, 0); // dtt-error {{requires @handle_open != 0}}
}

int main() {
    (void)variant_claim_after_close;
    return 0;
}
