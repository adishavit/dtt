// libdbus connection operations -- state violations (should FAIL to compile)
//
// Tests that DTT catches connection property and dispatch operations
// when @conn_state == 2 (closed) but the function requires @conn_state == 1.

#include <dbus/dbus.h>

// Variant 1: dbus_connection_read_write_dispatch on closed connection
void variant_read_write_dispatch_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    (void)dbus_connection_read_write_dispatch(conn, 1000);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: dbus_connection_pop_message on closed connection
void variant_pop_message_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    DBusMessage *msg = dbus_connection_pop_message(conn);
    (void)msg;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_read_write_dispatch_after_close();
    variant_pop_message_after_close();
    return 0;
}
