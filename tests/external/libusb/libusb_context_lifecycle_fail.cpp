// libusb context lifecycle -- use after exit (should NOT compile)
//
// Tests that DTT catches operations on a deinitialized context.
// After libusb_exit sets @ctx = 0, any function requiring @ctx != 0
// must be flagged as an error.

#include <libusb.h>

// Variant 1: handle_events after exit
void variant_events_after_exit() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);              // @ctx = 1
    libusb_exit(ctx);               // @ctx = 0

    // Requires @ctx != 0; this call must be rejected in enforce mode.
    libusb_handle_events(ctx); // dtt-error {{requires @ctx != 0}}
}

int main() {
    variant_events_after_exit();
    return 0;
}
