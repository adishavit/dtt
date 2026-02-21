// libdbus private connection -- operations after close (should FAIL to compile)
//
// Private connections obtained via dbus_bus_get_private must be closed by the
// caller before teardown. This test exercises a direct state violation:
// attempting to send on a closed private connection.

#include <dbus/dbus.h>

// Variant 1: Private connection -- close then send (state 2, requires 1)
void variant_private_send_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    // @conn_state = 1, @conn_type = 2

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    dbus_connection_close(conn);  // @conn_state = 2

    dbus_uint32_t serial;
    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    (void)dbus_connection_send(conn, msg, &serial);

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_private_send_after_close();
    return 0;
}
