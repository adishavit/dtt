// libdbus message lifecycle -- message misuse (should FAIL to compile)
//
// Tests that DTT catches message API calls where the preconditions are
// violated. Since dbus_message_unref does not explicitly set @msg_alive = 0
// in the .dtt annotations, these variants test _nonnull violations by
// passing nullptr where a non-null message is required.

#include <dbus/dbus.h>

// Variant 1: dbus_message_get_path with nullptr -- _nonnull violation
void variant_get_path_null() {
    DBusMessage *msg = nullptr;

    // Requires _nonnull != 0; this call must be rejected in enforce mode.
    (void)dbus_message_get_path(msg); // dtt-error {{requires _nonnull != 0}}
}

// Variant 2: dbus_message_new_method_return with nullptr -- _nonnull violation
void variant_method_return_null() {
    DBusMessage *msg = nullptr;

    // Requires _nonnull != 0; this call must be rejected in enforce mode.
    DBusMessage *reply = dbus_message_new_method_return(msg); // dtt-error {{requires _nonnull != 0}}
    (void)reply;
}

int main() {
    variant_get_path_null();
    variant_method_return_null();
    return 0;
}
