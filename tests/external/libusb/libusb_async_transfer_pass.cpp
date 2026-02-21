// libusb async transfer with cancel -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly track async transfer state transitions
// including alloc -> submit -> cancel -> free sequences.
// Covers: libusb_alloc_transfer, libusb_submit_transfer,
//   libusb_cancel_transfer, libusb_free_transfer

#include <libusb.h>

// Variant 1: alloc -> submit -> cancel (cancel requires @transfer_state == 2)
void variant_alloc_submit_cancel() {
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
                                                        // @transfer_state = 1
    libusb_submit_transfer(transfer);                   // requires @transfer_state == 1
                                                        // ensures @transfer_state = 2
    libusb_cancel_transfer(transfer);                   // requires @transfer_state == 2
}

// Variant 2: multiple transfers, one submitted and cancelled
void variant_multiple_with_cancel() {
    struct libusb_transfer *t1 = libusb_alloc_transfer(0);  // @transfer_state = 1
    struct libusb_transfer *t2 = libusb_alloc_transfer(0);  // @transfer_state = 1

    libusb_submit_transfer(t1);                         // t1: @transfer_state = 2
    libusb_cancel_transfer(t1);                         // t1: requires @transfer_state == 2

    libusb_free_transfer(t2);                           // t2: @transfer_state == 1, != 2 -- OK
}

int main() {
    variant_alloc_submit_cancel();
    variant_multiple_with_cancel();
    return 0;
}
