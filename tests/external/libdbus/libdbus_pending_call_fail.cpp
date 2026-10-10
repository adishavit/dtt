// libdbus pending call operations -- _nonnull violations (should FAIL to compile)
//
// Tests that DTT catches pending call operations on nullptr.

#include <dbus/dbus.h>

// Variant 1: dbus_pending_call_unref with nullptr
void variant_unref_null() {
    DBusPendingCall *pending = nullptr;

    // Requires _nonnull != 0; this call must be rejected in enforce mode.
    dbus_pending_call_unref(pending); // dtt-error {{requires _nonnull != 0}}
}

// Variant 2: dbus_pending_call_cancel with nullptr
void variant_cancel_null() {
    DBusPendingCall *pending = nullptr;

    // Requires _nonnull != 0; this call must be rejected in enforce mode.
    dbus_pending_call_cancel(pending); // dtt-error {{requires _nonnull != 0}}
}

int main() {
    variant_unref_null();
    variant_cancel_null();
    return 0;
}
