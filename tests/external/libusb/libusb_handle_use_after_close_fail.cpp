// libusb handle use-after-close -- operations on closed handle (should NOT compile)
//
// Tests that DTT catches I/O and interface operations performed after
// libusb_close has set @handle_open = 0.

#include <libusb.h>

// Variant 1: bulk_transfer after close
void variant_bulk_after_close(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                  // @handle_open = 1
    libusb_close(handle);                       // @handle_open = 0

    unsigned char buf[64];
    int transferred;
    // DTT ERROR: requires @handle_open != 0, but @handle_open == 0
    libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &transferred, 1000);
}

int main() {
    (void)variant_bulk_after_close;
    return 0;
}
