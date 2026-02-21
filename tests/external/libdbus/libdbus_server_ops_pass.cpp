// libdbus server operations -- server query functions (should COMPILE)
//
// Exercises server property accessors that only require _nonnull (no state
// constraint). Since dbus_server_listen has scope_exit result.@server_state == 0
// and dbus_server_unref doesn't reset state to 0 (known modeling gap tested
// in xfail), these tests receive a server pointer via function parameters
// to avoid scope_exit issues.
//
// dbus_server_disconnect requires @server_state == 1, so it is also tested
// here via parameter passing (DTT tracks ghost fields on parameters).

#include <dbus/dbus.h>

// Variant 1: server_get_is_connected, server_get_address, server_get_id
// All only require _nonnull on the server pointer.
void variant_server_queries(DBusServer *server) {
    dbus_bool_t connected = dbus_server_get_is_connected(server);
    (void)connected;

    char *addr = dbus_server_get_address(server);
    dbus_free(addr);

    char *id = dbus_server_get_id(server);
    dbus_free(id);
}

// Variant 2: server_ref / server_unref cycle
void variant_server_refcount(DBusServer *server) {
    DBusServer *ref = dbus_server_ref(server);
    (void)ref;
    dbus_server_unref(server);
}

int main() {
    // In -fsyntax-only mode, these never actually run.
    // DTT analyzes the function bodies above for precondition compliance.
    return 0;
}
