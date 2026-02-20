// libdbus bus operations -- state violations (should FAIL to compile)
//
// Tests that DTT catches bus operations on a closed connection where
// @conn_state == 2 but the function requires @conn_state == 1.

#include <dbus/dbus.h>

// Variant 1: dbus_bus_add_match on closed connection
void variant_add_match_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    dbus_bus_add_match(conn, "type='signal'", &err);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: dbus_bus_get_unique_name on closed connection
void variant_get_unique_name_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    const char *name = dbus_bus_get_unique_name(conn);
    (void)name;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_add_match_after_close();
    variant_get_unique_name_after_close();
    return 0;
}
