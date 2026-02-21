// libusb event handling variants -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost fields correctly allow event handling operations on
// an initialized context. All event handling functions require @ctx != 0.
// Covers: libusb_handle_events, libusb_handle_events_timeout,
//   libusb_handle_events_completed, libusb_handle_events_timeout_completed

#include <libusb.h>
#include <sys/time.h>

// Variant 1: handle_events_timeout
void variant_events_timeout() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    libusb_handle_events_timeout(ctx, &tv);             // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 2: handle_events_completed
void variant_events_completed() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    int completed = 0;
    libusb_handle_events_completed(ctx, &completed);    // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 3: handle_events_timeout_completed
void variant_events_timeout_completed() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    int completed = 0;
    libusb_handle_events_timeout_completed(ctx, &tv, &completed);
                                                        // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

// Variant 4: all event handling functions in one session
void variant_all_event_functions() {
    libusb_context *ctx = 0;
    libusb_init(&ctx);                                  // @ctx = 1

    libusb_handle_events(ctx);                          // requires @ctx != 0

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    libusb_handle_events_timeout(ctx, &tv);             // requires @ctx != 0

    int completed = 0;
    libusb_handle_events_completed(ctx, &completed);    // requires @ctx != 0

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    libusb_handle_events_timeout_completed(ctx, &tv, &completed);
                                                        // requires @ctx != 0

    libusb_exit(ctx);                                   // @ctx = 0
}

int main() {
    variant_events_timeout();
    variant_events_completed();
    variant_events_timeout_completed();
    variant_all_event_functions();
    return 0;
}
