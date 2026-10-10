// libusb transfer lifecycle -- free active transfer (should NOT compile)
//
// Tests that DTT catches libusb_free_transfer on a submitted (active)
// transfer. After libusb_submit_transfer sets @transfer_state = 2,
// libusb_free_transfer requires @transfer_state != 2.
// Freeing an active transfer is undefined behavior in libusb.

#include <libusb.h>

// Variant 1: alloc -> submit -> free (without cancel or completion)
void variant_free_active_transfer() {
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    if (!transfer) return;
                                                // @transfer_state = 1
    libusb_submit_transfer(transfer);           // requires @transfer_state == 1 -- satisfied
                                                // ensures @transfer_state = 2

    // Requires @transfer_state != 2; this call must be rejected in enforce mode.
    libusb_free_transfer(transfer); // dtt-error {{requires @transfer_state != 2}}
}

int main() {
    variant_free_active_transfer();
    return 0;
}
