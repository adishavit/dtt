// libdbus memory management and shutdown (should COMPILE)
//
// Exercises dbus_free, dbus_free_string_array, and dbus_shutdown.
// These functions have no preconditions in the .dtt annotations.

#include <dbus/dbus.h>

// Variant 1: dbus_free on bus-returned strings
void variant_free_bus_id() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    char *bus_id = dbus_bus_get_id(conn, &err);
    dbus_free(bus_id);

    char *server_id = dbus_connection_get_server_id(conn);
    dbus_free(server_id);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: dbus_free with nullptr (safe per D-Bus docs)
void variant_free_null() {
    dbus_free(nullptr);
}

// Variant 3: dbus_free_string_array with nullptr
void variant_free_string_array_null() {
    dbus_free_string_array(nullptr);
}

// Variant 4: dbus_shutdown
void variant_shutdown() {
    dbus_shutdown();
}

int main() {
    variant_free_bus_id();
    variant_free_null();
    variant_free_string_array_null();
    variant_shutdown();
    return 0;
}
