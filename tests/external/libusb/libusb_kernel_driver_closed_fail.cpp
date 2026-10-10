// libusb kernel driver ops on closed handle -- violations (should NOT compile)
//
// Tests that DTT catches kernel driver operations performed on a device
// handle that has been closed (@handle_open == 0).

#include <libusb.h>

// Variant 1: detach_kernel_driver on closed handle
void variant_detach_closed(libusb_device *dev) {
    if (!dev) return;
    libusb_device_handle *handle = nullptr;
    libusb_open(dev, &handle);                          // @handle_open = 1
    if (!handle) return;
    libusb_close(handle);                               // @handle_open = 0

    // Requires @handle_open != 0; this call must be rejected in enforce mode.
    libusb_detach_kernel_driver(handle, 0); // dtt-error {{requires @handle_open != 0}}
}

int main() {
    (void)variant_detach_closed;
    return 0;
}
