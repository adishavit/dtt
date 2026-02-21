// libdbus error operations -- all dbus_error_* functions (should COMPILE)
//
// Error objects have no ghost fields, only _nonnull preconditions.
// This test exercises init, free, is_set, has_name, and move_error.

#include <dbus/dbus.h>

// Variant 1: basic error lifecycle -- init, is_set, free
void variant_error_basic() {
    DBusError err;
    dbus_error_init(&err);

    dbus_bool_t is_set = dbus_error_is_set(&err);
    (void)is_set;

    dbus_error_free(&err);
}

// Variant 2: error with has_name check
void variant_error_has_name() {
    DBusError err;
    dbus_error_init(&err);

    // After init, no error is set, but we can still call has_name
    dbus_bool_t has = dbus_error_has_name(&err, DBUS_ERROR_NO_MEMORY);
    (void)has;

    dbus_error_free(&err);
}

// Variant 3: move_error between two error objects
void variant_error_move() {
    DBusError src;
    dbus_error_init(&src);

    DBusError dest;
    dbus_error_init(&dest);

    // Move error state from src to dest (even if no error is set, move is valid)
    dbus_move_error(&src, &dest);

    dbus_error_free(&dest);
    dbus_error_free(&src);
}

// Variant 4: error used with bus_get (typical pattern)
void variant_error_with_bus_get() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    if (dbus_error_is_set(&err)) {
        dbus_bool_t is_no_mem = dbus_error_has_name(&err, DBUS_ERROR_NO_MEMORY);
        (void)is_no_mem;
    }

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 5: multiple init/free cycles
void variant_error_reuse() {
    DBusError err;

    dbus_error_init(&err);
    (void)dbus_error_is_set(&err);
    dbus_error_free(&err);

    // Reinitialize and reuse
    dbus_error_init(&err);
    (void)dbus_error_has_name(&err, DBUS_ERROR_FAILED);
    dbus_error_free(&err);
}

int main() {
    variant_error_basic();
    variant_error_has_name();
    variant_error_move();
    variant_error_with_bus_get();
    variant_error_reuse();
    return 0;
}
