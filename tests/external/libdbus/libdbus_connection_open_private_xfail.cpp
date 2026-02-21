// libdbus connection_open_private -- XFAIL: scope_exit modeling gap
//
// dbus_connection_open_private has scope_exit result.@conn_state == 0, but
// dbus_connection_unref doesn't set @conn_state = 0 in the .dtt annotations.
// After close (state 2) + unref (no state change), scope_exit is unsatisfied.
// This is the same .dtt modeling gap as dbus_bus_get_private / dbus_server_listen.

#include <dbus/dbus.h>

// Variant 1: open_private -> close -> unref (correct usage, but DTT flags scope_exit)
void variant_open_private_lifecycle() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_connection_open_private(
        "tcp:host=localhost,port=12345", &err);
    // @conn_state = 1, @conn_type = 2

    dbus_connection_close(conn);    // @conn_state = 2
    dbus_connection_unref(conn);    // no state change -- @conn_state still 2

    dbus_error_free(&err);
    // scope_exit requires @conn_state == 0, but it's 2 => DTT error
}

int main() {
    variant_open_private_lifecycle();
    return 0;
}
