// libdbus connection operations -- all property/dispatch functions (should COMPILE)
//
// Exercises connection property accessors, dispatch functions, and message
// borrowing operations on a shared connection from dbus_bus_get.

#include <dbus/dbus.h>

// Variant 1: connection properties -- ref, is_connected, is_authenticated, is_anonymous, server_id
void variant_connection_properties() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1, @conn_type = 1

    // Bump refcount and later release
    DBusConnection *ref = dbus_connection_ref(conn);
    (void)ref;

    dbus_bool_t connected = dbus_connection_get_is_connected(conn);
    (void)connected;

    dbus_bool_t authenticated = dbus_connection_get_is_authenticated(conn);
    (void)authenticated;

    dbus_bool_t anonymous = dbus_connection_get_is_anonymous(conn);
    (void)anonymous;

    char *server_id = dbus_connection_get_server_id(conn);
    dbus_free(server_id);

    // Release the extra ref
    dbus_connection_unref(conn);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: unix fd and process id queries
void variant_connection_unix_info() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    int fd;
    dbus_bool_t got_fd = dbus_connection_get_unix_fd(conn, &fd);
    (void)got_fd;

    unsigned long pid;
    dbus_bool_t got_pid = dbus_connection_get_unix_process_id(conn, &pid);
    (void)got_pid;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 3: message size limits
void variant_connection_size_limits() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    dbus_connection_set_max_message_size(conn, 1024 * 1024);
    long max_msg = dbus_connection_get_max_message_size(conn);
    (void)max_msg;

    dbus_connection_set_max_received_size(conn, 2 * 1024 * 1024);
    long max_recv = dbus_connection_get_max_received_size(conn);
    (void)max_recv;

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 4: dispatch operations -- read_write, read_write_dispatch, dispatch
void variant_connection_dispatch() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    (void)dbus_connection_read_write(conn, 0);
    (void)dbus_connection_read_write_dispatch(conn, 0);
    (void)dbus_connection_dispatch(conn);

    dbus_connection_flush(conn);

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 5: message borrowing -- pop_message, borrow_message, return_message, steal_borrowed
void variant_connection_message_borrow() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    // Pop a message from the incoming queue (may return NULL at runtime)
    DBusMessage *popped = dbus_connection_pop_message(conn);
    // @msg_alive = 1 (per DTT ensures)

    dbus_message_unref(popped);

    // Borrow a message and return it
    DBusMessage *borrowed = dbus_connection_borrow_message(conn);
    // DTT doesn't track borrowed message state beyond the call

    if (borrowed) {
        dbus_connection_return_message(conn, borrowed);
    }

    // Borrow again and steal it
    DBusMessage *borrowed2 = dbus_connection_borrow_message(conn);
    if (borrowed2) {
        dbus_connection_steal_borrowed_message(conn, borrowed2);
        dbus_message_unref(borrowed2);
    }

    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_connection_properties();
    variant_connection_unix_info();
    variant_connection_size_limits();
    variant_connection_dispatch();
    variant_connection_message_borrow();
    return 0;
}
