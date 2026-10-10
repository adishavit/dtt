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
    if (!conn) { dbus_error_free(&err); return; }
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // Requires @conn_state == 1; this call must be rejected in enforce mode.
    dbus_bus_add_match(conn, "type='signal'", &err); // dtt-error {{requires @conn_state == 1}}

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: dbus_bus_get_unique_name on closed connection
void variant_get_unique_name_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) { dbus_error_free(&err); return; }
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // Requires @conn_state == 1; this call must be rejected in enforce mode.
    const char *name = dbus_bus_get_unique_name(conn); // dtt-error {{requires @conn_state == 1}}
    (void)name;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_add_match_after_close();
    variant_get_unique_name_after_close();
    return 0;
}
