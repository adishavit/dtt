// libdbus pending call operations -- all dbus_pending_call_* functions (should COMPILE)
//
// Exercises pending call ref/unref, notification, cancellation, completion
// check, and reply stealing. Uses a shared connection with send_with_reply
// to obtain a DBusPendingCall.

#include <dbus/dbus.h>

// Notification callback for set_notify
void pending_notify_cb(DBusPendingCall *pending, void *user_data) {
    (void)pending;
    (void)user_data;
}

// Variant 1: send_with_reply -> set_notify -> block -> get_completed -> steal_reply -> unref
void variant_pending_full_lifecycle() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Path",
        "com.example.Iface", "Query");
    // @msg_alive = 1

    DBusPendingCall *pending = (DBusPendingCall*)0x1;
    (void)dbus_connection_send_with_reply(conn, msg, &pending, 5000);
    // pending is now valid (DTT doesn't track pending_call ghost fields directly,
    // but _nonnull preconditions still apply)

    (void)dbus_pending_call_set_notify(pending, pending_notify_cb, nullptr, nullptr);

    dbus_pending_call_block(pending);

    dbus_bool_t completed = dbus_pending_call_get_completed(pending);
    (void)completed;

    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    // @msg_alive = 1
    dbus_message_unref(reply);

    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: send_with_reply -> cancel -> unref (cancelled before reply arrives)
void variant_pending_cancel() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Path",
        "com.example.Iface", "SlowOp");
    // @msg_alive = 1

    DBusPendingCall *pending = (DBusPendingCall*)0x1;
    (void)dbus_connection_send_with_reply(conn, msg, &pending, 1000);

    dbus_pending_call_cancel(pending);
    dbus_pending_call_unref(pending);

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 3: ref/unref on pending call
void variant_pending_refcount() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Path",
        "com.example.Iface", "Method");
    // @msg_alive = 1

    DBusPendingCall *pending = (DBusPendingCall*)0x1;
    (void)dbus_connection_send_with_reply(conn, msg, &pending, 5000);

    // Bump refcount
    DBusPendingCall *ref = dbus_pending_call_ref(pending);
    (void)ref;

    // Two unrefs: one for the ref, one for the original
    dbus_pending_call_unref(pending);
    dbus_pending_call_unref(pending);

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_pending_full_lifecycle();
    variant_pending_cancel();
    variant_pending_refcount();
    return 0;
}
