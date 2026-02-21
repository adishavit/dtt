// libusb_init_context -- modern context initialization (should COMPILE)
//
// Tests that DTT ghost fields correctly track the modern libusb_init_context
// API which replaces libusb_init. Also tests libusb_open_device_with_vid_pid
// which requires an initialized context.
// Covers: libusb_init_context, libusb_open_device_with_vid_pid

#include <libusb.h>
#include <stddef.h>

// Variant 1: init_context -> handle_events -> exit
void variant_init_context_basic() {
    libusb_context *ctx = 0;
    libusb_init_context(&ctx, NULL, 0);                 // ensures arg0.deref.@ctx = 1

    libusb_handle_events(ctx);                          // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 2: init_context -> open_device_with_vid_pid -> close -> exit
void variant_open_with_vid_pid() {
    libusb_context *ctx = 0;
    libusb_init_context(&ctx, NULL, 0);                 // @ctx = 1

    libusb_device_handle *handle =
        libusb_open_device_with_vid_pid(ctx, 0x1234, 0x5678);
                                                        // requires @ctx != 0

    if (handle) {
        libusb_close(handle);                           // @handle_open = 0
    }

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 3: init_context -> get_device_list -> free_device_list -> exit
void variant_init_context_with_enumeration() {
    libusb_context *ctx = 0;
    libusb_init_context(&ctx, NULL, 0);                 // @ctx = 1

    libusb_device **list;
    libusb_get_device_list(ctx, &list);                 // requires @ctx != 0
    libusb_free_device_list(list, 1);

    libusb_exit(ctx);                                   // @ctx = 0
}

int main() {
    variant_init_context_basic();
    variant_open_with_vid_pid();
    variant_init_context_with_enumeration();
    return 0;
}
