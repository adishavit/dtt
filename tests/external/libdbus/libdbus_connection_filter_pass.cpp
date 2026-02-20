// libdbus connection filter and object path operations (should COMPILE)
//
// Exercises add_filter, remove_filter, register_object_path,
// register_fallback, and unregister_object_path on a shared connection.

#include <dbus/dbus.h>

// Filter callback
DBusHandlerResult filter_func(DBusConnection *conn, DBusMessage *msg, void *user_data) {
    (void)conn;
    (void)msg;
    (void)user_data;
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Object path message handler
DBusHandlerResult path_handler(DBusConnection *conn, DBusMessage *msg, void *user_data) {
    (void)conn;
    (void)msg;
    (void)user_data;
    return DBUS_HANDLER_RESULT_HANDLED;
}

// Object path unregister callback
void path_unregister(DBusConnection *conn, void *user_data) {
    (void)conn;
    (void)user_data;
}

// Variant 1: add_filter -> remove_filter
void variant_filter_lifecycle() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    (void)dbus_connection_add_filter(conn, filter_func, nullptr, nullptr);
    dbus_connection_remove_filter(conn, filter_func, nullptr);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: register_object_path -> unregister_object_path
void variant_object_path_lifecycle() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusObjectPathVTable vtable;
    vtable.message_function = path_handler;
    vtable.unregister_function = path_unregister;

    (void)dbus_connection_register_object_path(
        conn, "/com/example/Object", &vtable, nullptr);

    (void)dbus_connection_unregister_object_path(
        conn, "/com/example/Object");

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 3: register_fallback -> unregister_object_path
void variant_fallback_lifecycle() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusObjectPathVTable vtable;
    vtable.message_function = path_handler;
    vtable.unregister_function = path_unregister;

    (void)dbus_connection_register_fallback(
        conn, "/com/example", &vtable, nullptr);

    (void)dbus_connection_unregister_object_path(
        conn, "/com/example");

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 4: signal subscription workflow
// bus_get -> add_match -> add_filter -> read_write_dispatch -> remove_filter -> remove_match -> unref
void variant_signal_subscription_workflow() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    const char *rule = "type='signal',interface='com.example.Signals'";

    dbus_bus_add_match(conn, rule, &err);

    (void)dbus_connection_add_filter(conn, filter_func, nullptr, nullptr);

    // Process messages briefly
    (void)dbus_connection_read_write_dispatch(conn, 0);

    dbus_connection_remove_filter(conn, filter_func, nullptr);

    dbus_bus_remove_match(conn, rule, &err);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 5: object path registration workflow
// bus_get -> register_object_path -> read_write -> unregister -> unref
void variant_object_path_workflow() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusObjectPathVTable vtable;
    vtable.message_function = path_handler;
    vtable.unregister_function = path_unregister;

    (void)dbus_connection_register_object_path(
        conn, "/com/example/Managed", &vtable, nullptr);

    // Process incoming messages
    (void)dbus_connection_read_write(conn, 0);
    (void)dbus_connection_dispatch(conn);

    (void)dbus_connection_unregister_object_path(
        conn, "/com/example/Managed");

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_filter_lifecycle();
    variant_object_path_lifecycle();
    variant_fallback_lifecycle();
    variant_signal_subscription_workflow();
    variant_object_path_workflow();
    return 0;
}
