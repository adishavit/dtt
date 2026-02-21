// libdbus connection send operations -- send, send_with_reply, send_with_reply_and_block (should COMPILE)
//
// Exercises all three send variants on a shared connection, including
// the full method-call-with-reply workflow.

#include <dbus/dbus.h>

// Variant 1: fire-and-forget -- dbus_connection_send
void variant_send_fire_and_forget() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_signal(
        "/com/example/Obj", "com.example.Iface", "Notify");
    // @msg_alive = 1

    dbus_uint32_t serial = 0;
    (void)dbus_connection_send(conn, msg, &serial);

    dbus_connection_flush(conn);

    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 2: async with pending -- dbus_connection_send_with_reply
void variant_send_with_reply() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Path",
        "com.example.Iface", "AsyncMethod");
    // @msg_alive = 1

    DBusPendingCall *pending = (DBusPendingCall*)0x1;
    (void)dbus_connection_send_with_reply(conn, msg, &pending, 5000);

    dbus_pending_call_block(pending);

    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    // @msg_alive = 1

    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 3: synchronous -- dbus_connection_send_with_reply_and_block
void variant_send_with_reply_and_block() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Path",
        "com.example.Iface", "SyncMethod");
    // @msg_alive = 1

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        conn, msg, 5000, &err);
    // @msg_alive = 1

    dbus_message_unref(reply);
    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

// Variant 4: method call with reply workflow
// bus_get -> message_new_method_call -> send_with_reply -> pending_call_steal_reply
// -> message_unref -> pending_call_unref -> unref
void variant_method_call_reply_workflow() {
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    // @conn_state = 1

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Service", "/com/example/Object",
        "com.example.Iface", "GetStatus");
    // @msg_alive = 1

    dbus_message_set_auto_start(msg, TRUE);

    DBusPendingCall *pending = (DBusPendingCall*)0x1;
    (void)dbus_connection_send_with_reply(conn, msg, &pending, 10000);

    dbus_pending_call_block(pending);

    dbus_bool_t done = dbus_pending_call_get_completed(pending);
    (void)done;

    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    // @msg_alive = 1

    // Inspect the reply
    int type = dbus_message_get_type(reply);
    (void)type;
    const char *sender = dbus_message_get_sender(reply);
    (void)sender;

    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
    dbus_message_unref(msg);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
}

int main() {
    variant_send_fire_and_forget();
    variant_send_with_reply();
    variant_send_with_reply_and_block();
    variant_method_call_reply_workflow();
    return 0;
}
