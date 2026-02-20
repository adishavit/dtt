// libusb kernel driver management -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly allow kernel driver operations on
// open device handles. All kernel driver functions require @handle_open != 0.
// Covers: libusb_kernel_driver_active, libusb_detach_kernel_driver,
//   libusb_attach_kernel_driver, libusb_set_auto_detach_kernel_driver

#include <libusb.h>

// Variant 1: init -> open -> check kernel driver -> detach -> claim -> release -> attach -> close -> exit
void variant_manual_detach_attach() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);

    libusb_device_handle *handle;
    libusb_open(list[0], &handle);                      // @handle_open = 1

    libusb_kernel_driver_active(handle, 0);             // requires @handle_open != 0
    libusb_detach_kernel_driver(handle, 0);             // requires @handle_open != 0
    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0
    libusb_release_interface(handle, 0);                // requires @handle_open != 0
    libusb_attach_kernel_driver(handle, 0);             // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 2: auto-detach mode
void variant_auto_detach() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);

    libusb_device_handle *handle;
    libusb_open(list[0], &handle);                      // @handle_open = 1

    libusb_set_auto_detach_kernel_driver(handle, 1);    // requires @handle_open != 0
    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0
    libusb_release_interface(handle, 0);                // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 3: all kernel driver functions in sequence on a standalone handle
void variant_all_kernel_ops(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    libusb_kernel_driver_active(handle, 0);             // requires @handle_open != 0
    libusb_detach_kernel_driver(handle, 0);             // requires @handle_open != 0
    libusb_attach_kernel_driver(handle, 0);             // requires @handle_open != 0
    libusb_set_auto_detach_kernel_driver(handle, 1);    // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
}

int main() {
    variant_manual_detach_attach();
    variant_auto_detach();
    (void)variant_all_kernel_ops;
    return 0;
}
