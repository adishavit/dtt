// libusb interface management -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly track interface claim/release and
// configuration operations on open device handles.
// Covers: libusb_claim_interface, libusb_release_interface,
//   libusb_set_interface_alt_setting, libusb_set_configuration,
//   libusb_clear_halt, libusb_reset_device

#include <libusb.h>

// Variant 1: open -> set_configuration -> claim -> release -> close
void variant_config_and_claim(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    libusb_set_configuration(handle, 1);                // requires @handle_open != 0
    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0
    libusb_release_interface(handle, 0);                // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 2: open -> claim -> set_alt_setting -> release -> close
void variant_alt_setting(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0
    libusb_set_interface_alt_setting(handle, 0, 1);     // requires @handle_open != 0
    libusb_release_interface(handle, 0);                // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 3: open -> claim -> clear_halt -> release -> close
void variant_clear_halt(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    libusb_claim_interface(handle, 0);
    libusb_clear_halt(handle, 0x81);                    // not annotated, no DTT constraint
    libusb_release_interface(handle, 0);

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 4: open -> reset_device -> close
void variant_reset_device(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    libusb_reset_device(handle);                        // not annotated, no DTT constraint

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 5: full workflow: init -> enumerate -> open -> config -> claim -> IO -> release -> close -> exit
void variant_full_workflow() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    libusb_device_handle *handle;
    libusb_open(list[0], &handle);                      // @handle_open = 1

    libusb_set_configuration(handle, 1);                // requires @handle_open != 0
    libusb_claim_interface(handle, 0);                  // requires @handle_open != 0
    libusb_set_interface_alt_setting(handle, 0, 0);     // requires @handle_open != 0
    libusb_release_interface(handle, 0);                // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);                                   // @ctx = 0
}

int main() {
    (void)variant_config_and_claim;
    (void)variant_alt_setting;
    (void)variant_clear_halt;
    (void)variant_reset_device;
    variant_full_workflow();
    return 0;
}
