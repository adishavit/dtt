// libdbus bus operations -- all dbus_bus_* functions (should COMPILE)
//
// Exercises every annotated dbus_bus_* function using a shared connection
// obtained from dbus_bus_get. Shared connections have no scope_exit issues.

#include <dbus/dbus.h>

// Variant 1: get_unique_name, get_unix_user, get_id
void variant_bus_query_functions() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1, @conn_type = 1

    // Query the unique bus name assigned to this connection
    const char *name = dbus_bus_get_unique_name(conn);
    (void)name;

    // Query the unix user ID of a named connection
    unsigned long uid = dbus_bus_get_unix_user(conn, "org.freedesktop.DBus", &err);
    (void)uid;

    // Get the bus daemon's machine ID
    char *id = dbus_bus_get_id(conn, &err);
    dbus_free(id);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: add_match, remove_match
void variant_bus_match_rules() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    const char *rule = "type='signal',interface='com.example.Interface'";

    dbus_bus_add_match(conn, rule, &err);
    dbus_bus_remove_match(conn, rule, &err);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 3: request_name, release_name, name_has_owner
void variant_bus_name_management() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    int result = dbus_bus_request_name(conn,
        "com.example.TestService", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    (void)result;

    dbus_bool_t has_owner = dbus_bus_name_has_owner(
        conn, "com.example.TestService", &err);
    (void)has_owner;

    int release_result = dbus_bus_release_name(
        conn, "com.example.TestService", &err);
    (void)release_result;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 4: register (on an already-connected bus)
void variant_bus_register() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    (void)dbus_bus_register(conn, &err);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 5: full workflow -- register, request_name, query, add_match, remove_match, release
void variant_bus_full_workflow() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    (void)dbus_bus_register(conn, &err);

    int req = dbus_bus_request_name(conn,
        "com.example.WorkflowSvc", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    (void)req;

    const char *unique = dbus_bus_get_unique_name(conn);
    (void)unique;

    unsigned long uid = dbus_bus_get_unix_user(conn, "org.freedesktop.DBus", &err);
    (void)uid;

    char *bus_id = dbus_bus_get_id(conn, &err);
    dbus_free(bus_id);

    dbus_bool_t owned = dbus_bus_name_has_owner(conn, "com.example.WorkflowSvc", &err);
    (void)owned;

    const char *rule = "type='signal',sender='com.example.WorkflowSvc'";
    dbus_bus_add_match(conn, rule, &err);
    dbus_bus_remove_match(conn, rule, &err);

    int rel = dbus_bus_release_name(conn, "com.example.WorkflowSvc", &err);
    (void)rel;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_bus_query_functions();
    variant_bus_match_rules();
    variant_bus_name_management();
    variant_bus_register();
    variant_bus_full_workflow();
    return 0;
}
