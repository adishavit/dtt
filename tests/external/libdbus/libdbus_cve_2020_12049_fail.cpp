// CVE-2020-12049: File descriptor leak via private connection use after close
// (should FAIL to compile)
//
// CVE-2020-12049 involves a file descriptor leak in
// _dbus_read_socket_with_unix_fds triggered when I/O operations continue
// on a connection that has already been closed. The leak occurs because
// the connection's transport layer has been shut down but the caller
// still attempts read operations.
// DTT catches this because dbus_connection_read_write requires @conn_state == 1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2020-12049

#include <dbus/dbus.h>

// Variant 1: read_write on closed private connection -- FD leak pattern
void variant_read_write_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) { dbus_error_free(&err); return; }
    // @conn_state = 1, @conn_type = 2

    dbus_connection_close(conn);  // @conn_state = 2

    // Requires @conn_state == 1; this call must be rejected in enforce mode.
    (void)dbus_connection_read_write(conn, 1000); // dtt-error {{requires @conn_state == 1}}

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_read_write_after_close();
    return 0;
}
