// libusb interface ops on closed handle -- various violations (should NOT compile)
//
// Tests that DTT catches interface and configuration operations performed
// on a device handle that has been closed (@handle_open == 0).

#include <libusb.h>

// Variant 1: set_configuration on closed handle
void variant_set_config_closed(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1
    libusb_close(handle);                               // @handle_open = 0

    // DTT ERROR: requires @handle_open != 0, but @handle_open == 0
    libusb_set_configuration(handle, 1);
}

int main() {
    (void)variant_set_config_closed;
    return 0;
}
