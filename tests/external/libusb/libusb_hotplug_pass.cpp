// libusb hotplug callback management -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly allow hotplug registration and
// deregistration on an initialized context. Both hotplug functions require
// @ctx != 0.
// Covers: libusb_hotplug_register_callback, libusb_hotplug_deregister_callback

#include <libusb.h>
#include <stddef.h>

// Stub callback for hotplug registration (syntax-only, never executed)
static int LIBUSB_CALL hotplug_cb(libusb_context *ctx,
                                  libusb_device *device,
                                  libusb_hotplug_event event,
                                  void *user_data) {
    (void)ctx; (void)device; (void)event; (void)user_data;
    return 0;
}

// Variant 1: init -> register_callback -> handle_events -> deregister -> exit
void variant_register_deregister() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_hotplug_callback_handle cb_handle;
    libusb_hotplug_register_callback(ctx,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
        0,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_cb, NULL,
        &cb_handle);                                    // requires @ctx != 0

    libusb_handle_events(ctx);                          // requires @ctx != 0

    libusb_hotplug_deregister_callback(ctx, cb_handle); // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 2: register for both arrive and leave events
void variant_both_events() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_hotplug_callback_handle cb_handle;
    libusb_hotplug_register_callback(ctx,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        LIBUSB_HOTPLUG_ENUMERATE,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_cb, NULL,
        &cb_handle);                                    // requires @ctx != 0

    libusb_hotplug_deregister_callback(ctx, cb_handle); // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 3: multiple registrations
void variant_multiple_registrations() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_hotplug_callback_handle cb1, cb2;
    libusb_hotplug_register_callback(ctx,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, 0,
        LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_cb, NULL, &cb1);                        // requires @ctx != 0

    libusb_hotplug_register_callback(ctx,
        LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0,
        LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_cb, NULL, &cb2);                        // requires @ctx != 0

    libusb_hotplug_deregister_callback(ctx, cb1);       // requires @ctx != 0
    libusb_hotplug_deregister_callback(ctx, cb2);       // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

int main() {
    variant_register_deregister();
    variant_both_events();
    variant_multiple_registrations();
    return 0;
}
