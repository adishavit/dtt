// libdbus message operations -- _nonnull violations (should FAIL to compile)
//
// Tests that DTT catches message operations on nullptr messages and
// on messages passed to functions requiring _nonnull.

#include <dbus/dbus.h>

// Variant 1: dbus_message_copy with nullptr
void variant_copy_null() {
    DBusMessage *msg = nullptr;

    // DTT ERROR: requires arg0._nonnull != 0, but msg is nullptr
    DBusMessage *copy = dbus_message_copy(msg);
    (void)copy;
}

// Variant 2: dbus_message_ref with nullptr
void variant_ref_null() {
    DBusMessage *msg = nullptr;

    // DTT ERROR: requires arg0._nonnull != 0, but msg is nullptr
    DBusMessage *ref = dbus_message_ref(msg);
    (void)ref;
}

// Variant 3: dbus_message_set_destination with nullptr message
void variant_set_dest_null() {
    DBusMessage *msg = nullptr;

    // DTT ERROR: requires arg0._nonnull != 0, but msg is nullptr
    (void)dbus_message_set_destination(msg, "com.example.Dest");
}

// Variant 4: dbus_message_new_error with nullptr reply_to
void variant_new_error_null_reply() {
    DBusMessage *reply_to = nullptr;

    // DTT ERROR: requires arg0._nonnull != 0, but reply_to is nullptr
    DBusMessage *err_msg = dbus_message_new_error(
        reply_to, "com.example.Error", "error text");
    (void)err_msg;
}

int main() {
    variant_copy_null();
    variant_ref_null();
    variant_set_dest_null();
    variant_new_error_null_reply();
    return 0;
}
