// libdbus message lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT tracks the @msg_alive ghost field through message creation,
// accessor calls, and unref. Each variant creates a message, uses it, and
// properly unrefs it.

#include <dbus/dbus.h>

// Variant 1: new_method_call -> set_path -> append_args -> unref
void variant_method_call_with_args() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/com/example/Object",
        "com.example.Interface", "Execute");  // @msg_alive = 1

    (void)dbus_message_set_path(msg, "/com/example/NewPath");  // requires @msg_alive != 0

    const char *arg_value = "hello";
    (void)dbus_message_append_args(msg,
        DBUS_TYPE_STRING, &arg_value,
        DBUS_TYPE_INVALID);                    // requires @msg_alive != 0

    dbus_message_unref(msg);                   // requires @msg_alive != 0
}

// Variant 2: new_signal -> get_type -> unref
void variant_signal_lifecycle() {
    DBusMessage *msg = dbus_message_new_signal(
        "/com/example/Object",
        "com.example.Interface",
        "StatusChanged");                      // @msg_alive = 1

    (void)dbus_message_get_type(msg);          // requires @msg_alive != 0

    dbus_message_unref(msg);                   // requires @msg_alive != 0
}

// Variant 3: new_method_call -> send_with_reply_and_block -> unref reply -> unref original
void variant_send_and_get_reply() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Path",
        "com.example.Iface", "Query");         // @msg_alive = 1

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        conn, msg, -1, &err);                  // requires @conn_state == 1, @msg_alive != 0
                                               // reply: @msg_alive = 1

    dbus_message_unref(reply);                 // requires @msg_alive != 0
    dbus_message_unref(msg);                   // requires @msg_alive != 0
    dbus_connection_unref(conn);               // requires @conn_state != 0
    dbus_error_free(&err);
}

int main() {
    variant_method_call_with_args();
    variant_signal_lifecycle();
    variant_send_and_get_reply();
    return 0;
}
