// libusb context lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that the libusb_init -> use -> libusb_exit lifecycle is tracked
// correctly by DTT ghost field @ctx. libusb_init sets @ctx = 1 via
// arg0.deref, and libusb_exit sets @ctx = 0. Functions like
// libusb_handle_events and libusb_get_device_list require @ctx != 0.

#include <libusb.h>

// Variant 1: init -> handle_events -> exit
void variant_init_events_exit() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);              // ensures arg0.deref.@ctx = 1
    libusb_handle_events(ctx);      // requires @ctx != 0 -- satisfied
    libusb_exit(ctx);               // requires @ctx != 0 -- satisfied; ensures @ctx = 0
}

// Variant 2: init -> get_device_list -> free_device_list -> exit
void variant_init_devlist_exit() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                            // @ctx = 1

    libusb_device **list = (libusb_device**)0x1;
    libusb_get_device_list(ctx, &list);           // requires @ctx != 0 -- satisfied
    libusb_free_device_list(list, 1);             // requires arg0._nonnull != 0

    libusb_exit(ctx);                             // @ctx = 0
}

int main() {
    variant_init_events_exit();
    variant_init_devlist_exit();
    return 0;
}
