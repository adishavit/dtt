// libusb descriptor access after context exit -- violations (should NOT compile)
//
// Tests that DTT catches device list operations after the context has been
// deinitialized (@ctx == 0). libusb_get_device_list requires @ctx != 0.

#include <libusb.h>

// Variant 1: get_device_list after exit
void variant_devlist_after_exit() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1
    libusb_exit(ctx);                                   // @ctx = 0

    libusb_device **list;
    // Requires @ctx != 0; this call must be rejected in enforce mode.
    libusb_get_device_list(ctx, &list); // dtt-error {{requires @ctx != 0}}
}

int main() {
    variant_devlist_after_exit();
    return 0;
}
