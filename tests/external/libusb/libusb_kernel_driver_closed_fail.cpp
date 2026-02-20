// libusb kernel driver ops on closed handle -- violations (should NOT compile)
//
// Tests that DTT catches kernel driver operations performed on a device
// handle that has been closed (@handle_open == 0).

#include <libusb.h>

// Variant 1: detach_kernel_driver on closed handle
void variant_detach_closed(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1
    libusb_close(handle);                               // @handle_open = 0

    // DTT ERROR: requires @handle_open != 0, but @handle_open == 0
    libusb_detach_kernel_driver(handle, 0);
}

int main() {
    (void)variant_detach_closed;
    return 0;
}
