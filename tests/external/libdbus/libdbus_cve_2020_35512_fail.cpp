// CVE-2020-35512: Use-after-free via connection dispatch after close
// (should FAIL to compile)
//
// CVE-2020-35512 involves a use-after-free in _dbus_list_unlink triggered
// when connection operations continue after the connection has been closed.
// The underlying bug stems from accessing connection internals after
// the connection state transitions away from "connected."
// DTT catches this because dbus_connection_dispatch requires @conn_state == 1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2020-35512

#include <dbus/dbus.h>

// Variant 1: dispatch after close -- use-after-free of connection internals
void variant_dispatch_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1, but @conn_state == 2
    (void)dbus_connection_dispatch(conn);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_dispatch_after_close();
    return 0;
}
