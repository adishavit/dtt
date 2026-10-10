// libdbus server operations -- state violations (should FAIL to compile)
//
// Tests that DTT catches server operations when @server_state == 2
// (disconnected) but the function requires @server_state == 1.

#include <dbus/dbus.h>

// Dummy callback
void on_new_conn(DBusServer *server, DBusConnection *conn, void *data) {
    (void)server;
    (void)conn;
    (void)data;
}

// Variant 1: set_new_connection_function after disconnect
void variant_set_handler_after_disconnect() {
    DBusError err;
    dbus_error_init(&err);

    DBusServer *server = dbus_server_listen("tcp:host=localhost,port=0", &err);
    if (!server) { dbus_error_free(&err); return; }
    // @server_state = 1

    dbus_server_disconnect(server);  // @server_state = 2

    // Requires @server_state == 1; this call must be rejected in enforce mode.
    dbus_server_set_new_connection_function(server, on_new_conn, nullptr, nullptr); // dtt-error {{requires @server_state == 1}}

    dbus_server_unref(server);
    dbus_error_free(&err);
}

// Variant 2: double disconnect
void variant_double_disconnect() {
    DBusError err;
    dbus_error_init(&err);

    DBusServer *server = dbus_server_listen("tcp:host=localhost,port=0", &err);
    if (!server) { dbus_error_free(&err); return; }
    // @server_state = 1

    dbus_server_disconnect(server);  // @server_state = 2

    // Requires @server_state == 1; this call must be rejected in enforce mode.
    dbus_server_disconnect(server); // dtt-error {{requires @server_state == 1}}

    dbus_server_unref(server);
    dbus_error_free(&err);
}

int main() {
    variant_set_handler_after_disconnect();
    variant_double_disconnect();
    return 0;
}
