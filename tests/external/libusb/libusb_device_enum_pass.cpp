// libusb device enumeration -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly track device enumeration workflows:
// device list retrieval, descriptor access, reference counting, and cleanup.
// Covers: libusb_get_device_list, libusb_free_device_list,
//   libusb_get_device_descriptor, libusb_get_config_descriptor,
//   libusb_free_config_descriptor, libusb_get_active_config_descriptor,
//   libusb_get_bos_descriptor, libusb_free_bos_descriptor,
//   libusb_get_string_descriptor_ascii, libusb_ref_device, libusb_unref_device

#include <libusb.h>

// Variant 1: init -> get_device_list -> get_device_descriptor -> free_list -> exit
void variant_device_descriptor() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(list[0], &desc);       // requires arg0._nonnull != 0

    libusb_free_device_list(list, 1);                   // requires arg0._nonnull != 0
    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 2: init -> get_device_list -> get_config_descriptor -> free_config -> free_list -> exit
void variant_config_descriptor() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    struct libusb_config_descriptor *config;
    libusb_get_config_descriptor(list[0], 0, &config);  // requires arg0._nonnull != 0
    libusb_free_config_descriptor(config);              // requires arg0._nonnull != 0

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}

// Variant 3: init -> get_device_list -> get_active_config_descriptor -> free_config -> free_list -> exit
void variant_active_config_descriptor() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    struct libusb_config_descriptor *config;
    libusb_get_active_config_descriptor(list[0], &config);  // requires arg0._nonnull != 0
    libusb_free_config_descriptor(config);              // requires arg0._nonnull != 0

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
}

// Variant 4: ref_device / unref_device to manage device lifetime beyond list
void variant_ref_unref() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0

    libusb_device *dev = libusb_ref_device(list[0]);    // requires arg0._nonnull != 0
    libusb_free_device_list(list, 0);                   // free list without unreffing

    libusb_unref_device(dev);                           // requires arg0._nonnull != 0

    libusb_exit(ctx);
}

// Variant 5: BOS descriptor via open handle
void variant_bos_descriptor(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    struct libusb_bos_descriptor *bos;
    libusb_get_bos_descriptor(handle, &bos);            // requires @handle_open != 0
    libusb_free_bos_descriptor(bos);                    // requires arg0._nonnull != 0

    libusb_close(handle);                               // @handle_open = 0
}

// Variant 6: string descriptor via open handle
void variant_string_descriptor(libusb_device *dev) {
    libusb_device_handle *handle;
    libusb_open(dev, &handle);                          // @handle_open = 1

    unsigned char buf[256];
    libusb_get_string_descriptor_ascii(handle, 1, buf, sizeof(buf));
                                                        // requires @handle_open != 0

    libusb_close(handle);                               // @handle_open = 0
}

int main() {
    variant_device_descriptor();
    variant_config_descriptor();
    variant_active_config_descriptor();
    variant_ref_unref();
    (void)variant_bos_descriptor;
    (void)variant_string_descriptor;
    return 0;
}
