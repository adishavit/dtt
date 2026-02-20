// libdbus server lifecycle -- XFAIL: scope_exit modeling gap
//
// dbus_server_listen has scope_exit result.@server_state == 0, but
// dbus_server_unref doesn't set @server_state = 0 in the .dtt annotations.
// After disconnect (state 2) + unref (no state change), scope_exit is
// unsatisfied. This is a .dtt modeling gap — correct code that DTT flags.

#include <dbus/dbus.h>

// Dummy new-connection callback
void on_new_connection(DBusServer *, DBusConnection *, void *) {}

// Variant 1: listen -> set_new_connection_function -> disconnect -> unref
void variant_server_full_lifecycle() {
    DBusError err;
    dbus_error_init(&err);

    DBusServer *server = dbus_server_listen(
        "tcp:host=localhost,port=0", &err);     // @server_state = 1

    dbus_server_set_new_connection_function(     // requires @server_state == 1
        server, on_new_connection, nullptr, nullptr);

    dbus_server_disconnect(server);              // requires @server_state == 1; ensures @server_state = 2
    dbus_server_unref(server);

    dbus_error_free(&err);
}

int main() {
    variant_server_full_lifecycle();
    return 0;
}
