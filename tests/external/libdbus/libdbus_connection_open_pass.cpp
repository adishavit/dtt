// libdbus connection_open -- shared connection via address (should COMPILE)
//
// Exercises dbus_connection_open (shared, no scope_exit) and
// dbus_connection_open_private (private, has scope_exit -- tested in xfail).
// This test only uses the shared variant.

#include <dbus/dbus.h>

// Variant 1: connection_open (shared) -> use -> unref
void variant_open_shared() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_connection_open(
        "tcp:host=localhost,port=12345", &err);
    // @conn_state = 1, @conn_type = 1

    dbus_bool_t connected = dbus_connection_get_is_connected(conn);
    (void)connected;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: connection_open -> read_write -> dispatch -> unref
void variant_open_and_dispatch() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_connection_open(
        "unix:path=/tmp/dbus-test", &err);
    // @conn_state = 1

    (void)dbus_connection_read_write(conn, 0);
    (void)dbus_connection_dispatch(conn);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_open_shared();
    variant_open_and_dispatch();
    return 0;
}
