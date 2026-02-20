// libdbus connection lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT tracks connection ghost fields (@conn_state, @conn_type)
// correctly through the shared bus acquisition -> use -> teardown lifecycle.
// Note: private connections (dbus_bus_get_private) have a scope_exit modeling
// gap — dbus_connection_unref doesn't reset @conn_state to 0. See the
// separate xfail test for that pattern.

#include <dbus/dbus.h>

// Variant 1: Shared connection -- bus_get -> register -> request_name -> send -> flush -> unref
void variant_shared_connection() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1, @conn_type = 1

    (void)dbus_bus_register(conn, &err);         // requires @conn_state == 1

    (void)dbus_bus_request_name(conn,             // requires @conn_state == 1
        "com.example.Test", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/com/example/Object",
        "com.example.Interface", "DoSomething");  // @msg_alive = 1

    dbus_uint32_t serial;
    (void)dbus_connection_send(conn, msg, &serial);  // requires @conn_state == 1, @msg_alive != 0
    dbus_connection_flush(conn);                      // requires @conn_state == 1

    dbus_message_unref(msg);                          // requires @msg_alive != 0
    dbus_connection_unref(conn);                      // requires @conn_state != 0

    dbus_error_free(&err);
}

int main() {
    variant_shared_connection();
    return 0;
}
