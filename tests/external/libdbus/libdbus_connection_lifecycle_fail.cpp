// libdbus connection lifecycle -- operations after close (should FAIL to compile)
//
// Tests that DTT catches connection operations when @conn_state == 2 (closed)
// but the function requires @conn_state == 1 (open/connected).

#include <dbus/dbus.h>

// Variant 1: dbus_connection_send after close
void variant_send_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) { dbus_error_free(&err); return; }
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    if (!msg) {
        dbus_connection_unref(conn);
        dbus_error_free(&err);
        return;
    }
    // @msg_alive = 1

    dbus_connection_close(conn);  // @conn_state = 2

    dbus_uint32_t serial;
    // Requires @conn_state == 1; this call must be rejected in enforce mode.
    (void)dbus_connection_send(conn, msg, &serial); // dtt-error {{requires @conn_state == 1}}

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: dbus_connection_flush after close
void variant_flush_after_close() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) { dbus_error_free(&err); return; }
    // @conn_state = 1

    dbus_connection_close(conn);  // @conn_state = 2

    // Requires @conn_state == 1; this call must be rejected in enforce mode.
    dbus_connection_flush(conn); // dtt-error {{requires @conn_state == 1}}

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_send_after_close();
    variant_flush_after_close();
    return 0;
}
