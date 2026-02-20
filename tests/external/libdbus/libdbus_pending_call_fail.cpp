// libdbus pending call operations -- _nonnull violations (should FAIL to compile)
//
// Tests that DTT catches pending call operations on nullptr.

#include <dbus/dbus.h>

// Variant 1: dbus_pending_call_unref with nullptr
void variant_unref_null() {
    DBusPendingCall *pending = nullptr;

    // DTT ERROR: requires arg0._nonnull != 0, but pending is nullptr
    dbus_pending_call_unref(pending);
}

// Variant 2: dbus_pending_call_cancel with nullptr
void variant_cancel_null() {
    DBusPendingCall *pending = nullptr;

    // DTT ERROR: requires arg0._nonnull != 0, but pending is nullptr
    dbus_pending_call_cancel(pending);
}

int main() {
    variant_unref_null();
    variant_cancel_null();
    return 0;
}
