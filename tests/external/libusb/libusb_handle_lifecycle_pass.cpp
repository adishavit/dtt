// libusb device handle lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost field @handle_open correctly tracks device handle
// state through open -> claim -> I/O -> release -> close sequences.
// libusb_open sets @handle_open = 1 via arg1.deref; libusb_close requires
// @handle_open != 0 and sets it to 0. Interface and I/O operations all
// require @handle_open != 0.

#include <libusb.h>
#include <stddef.h>

// Variant 1: open -> claim_interface -> bulk_transfer -> release_interface -> close
void variant_bulk_io(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                  // ensures arg1.deref.@handle_open = 1

    libusb_claim_interface(handle, 0);          // requires @handle_open != 0 -- satisfied

    unsigned char buf[64];
    int transferred;
    libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &transferred, 1000);
                                                // requires @handle_open != 0 -- satisfied

    libusb_release_interface(handle, 0);        // requires @handle_open != 0 -- satisfied
    libusb_close(handle);                       // requires @handle_open != 0 -- satisfied; @handle_open = 0
}

// Variant 2: open -> control_transfer -> close
void variant_control_io(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                  // @handle_open = 1

    unsigned char buf[256];
    libusb_control_transfer(handle, 0x80, 0x06, 0x0100, 0, buf, sizeof(buf), 1000);
                                                // requires @handle_open != 0 -- satisfied

    libusb_close(handle);                       // @handle_open = 0
}

int main() {
    // These functions accept a libusb_device*; in a real program it comes
    // from libusb_get_device_list. Here we only test handle lifecycle tracking,
    // so the device pointer is opaque -- syntax-only compilation never dereferences it.
    (void)variant_bulk_io;
    (void)variant_control_io;
    return 0;
}
