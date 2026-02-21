// libdbus send operations -- state violations (should FAIL to compile)
//
// Tests that DTT catches send_with_reply and send_with_reply_and_block
// on a closed connection.

#include <dbus/dbus.h>

// Variant 1: send_with_reply on closed connection
void variant_send_with_reply_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    dbus_connection_close(conn);  // @conn_state = 2

    DBusPendingCall *pending = nullptr;
    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    (void)dbus_connection_send_with_reply(conn, msg, &pending, 5000);

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: send_with_reply_and_block on closed connection
void variant_send_block_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        conn, msg, 5000, &err);
    (void)reply;

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_send_with_reply_after_close();
    variant_send_block_after_close();
    return 0;
}
