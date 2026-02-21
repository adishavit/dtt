// libusb synchronous I/O -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly allow synchronous transfer operations
// on open device handles. All sync I/O functions require @handle_open != 0.
// Covers: libusb_control_transfer, libusb_bulk_transfer,
//   libusb_interrupt_transfer

#include <libusb.h>

// Variant 1: open -> interrupt_transfer -> close
void variant_interrupt_transfer(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    unsigned char buf[8];
    int transferred;
    libusb_interrupt_transfer(handle, 0x81, buf, sizeof(buf), &transferred, 1000);
                                                        // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 2: all three sync transfer types in one session
void variant_all_sync_transfers(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    // control transfer
    unsigned char ctrl_buf[256];
    libusb_control_transfer(handle, 0x80, 0x06, 0x0100, 0,
                            ctrl_buf, sizeof(ctrl_buf), 1000);
                                                        // requires @handle_open != 0

    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0

    // bulk transfer
    unsigned char bulk_buf[512];
    int bulk_transferred;
    libusb_bulk_transfer(handle, 0x02, bulk_buf, sizeof(bulk_buf),
                         &bulk_transferred, 5000);
                                                        // requires @handle_open != 0

    // interrupt transfer
    unsigned char intr_buf[8];
    int intr_transferred;
    libusb_interrupt_transfer(handle, 0x83, intr_buf, sizeof(intr_buf),
                              &intr_transferred, 1000);
                                                        // requires @handle_open != 0

    libusb_release_interface(handle, 0);                // requires @handle_open != 0
    libusb_close(handle);                               // @handle_open = 0
}

// Variant 3: full pipeline -- init -> enumerate -> open -> claim -> transfer -> release -> close -> exit
void variant_full_sync_pipeline() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    libusb_device_handle *handle;
    libusb_open(list[0], &handle);                      // @handle_open = 1

    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0

    unsigned char buf[64];
    int transferred;
    libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf),
                         &transferred, 1000);           // requires @handle_open != 0

    libusb_interrupt_transfer(handle, 0x83, buf, 8,
                              &transferred, 500);       // requires @handle_open != 0

    libusb_release_interface(handle, 0);                // requires @handle_open != 0
    libusb_close(handle);                               // @handle_open = 0

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);                                   // @ctx = 0
}

int main() {
    (void)variant_interrupt_transfer;
    (void)variant_all_sync_transfers;
    variant_full_sync_pipeline();
    return 0;
}
