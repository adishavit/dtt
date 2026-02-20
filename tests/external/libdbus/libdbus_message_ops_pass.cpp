// libdbus message operations -- all dbus_message_* functions (should COMPILE)
//
// Exercises every annotated dbus_message_* accessor, creator, and query
// function. Each variant creates a message, manipulates it, and properly
// unrefs when done.

#include <dbus/dbus.h>

// Variant 1: method call -- all setter/getter pairs
void variant_method_call_accessors() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/com/example/Obj",
        "com.example.Iface", "DoWork");
    // @msg_alive = 1

    // Path
    (void)dbus_message_set_path(msg, "/com/example/NewObj");
    const char *path = dbus_message_get_path(msg);
    (void)path;

    // Interface
    (void)dbus_message_set_interface(msg, "com.example.NewIface");
    const char *iface = dbus_message_get_interface(msg);
    (void)iface;

    // Member
    (void)dbus_message_set_member(msg, "NewMethod");
    const char *member = dbus_message_get_member(msg);
    (void)member;

    // Destination
    (void)dbus_message_set_destination(msg, "com.example.NewDest");
    const char *dest = dbus_message_get_destination(msg);
    (void)dest;

    // Sender
    (void)dbus_message_set_sender(msg, ":1.42");
    const char *sender = dbus_message_get_sender(msg);
    (void)sender;

    // Error name (valid on any message type)
    (void)dbus_message_set_error_name(msg, "com.example.Error.Oops");
    const char *err_name = dbus_message_get_error_name(msg);
    (void)err_name;

    // Signature (read-only)
    const char *sig = dbus_message_get_signature(msg);
    (void)sig;

    dbus_message_unref(msg);
}

// Variant 2: serial and reply serial
void variant_serial_operations() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    dbus_uint32_t serial = dbus_message_get_serial(msg);
    (void)serial;

    (void)dbus_message_set_reply_serial(msg, 42);
    dbus_uint32_t reply_serial = dbus_message_get_reply_serial(msg);
    (void)reply_serial;

    dbus_message_unref(msg);
}

// Variant 3: auto-start flag
void variant_auto_start() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    dbus_message_set_auto_start(msg, TRUE);
    dbus_bool_t auto_start = dbus_message_get_auto_start(msg);
    (void)auto_start;

    dbus_message_unref(msg);
}

// Variant 4: type queries -- is_method_call, is_signal, is_error
void variant_type_queries() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    int type = dbus_message_get_type(msg);
    (void)type;

    dbus_bool_t is_call = dbus_message_is_method_call(msg, "com.example.Iface", "Method");
    (void)is_call;

    dbus_bool_t is_sig = dbus_message_is_signal(msg, "com.example.Iface", "SomeSignal");
    (void)is_sig;

    dbus_bool_t is_err = dbus_message_is_error(msg, "com.example.Error");
    (void)is_err;

    dbus_message_unref(msg);
}

// Variant 5: has_destination, has_sender, has_signature
void variant_has_queries() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    dbus_bool_t has_dest = dbus_message_has_destination(msg, "com.example.Dest");
    (void)has_dest;

    dbus_bool_t has_sender = dbus_message_has_sender(msg, ":1.0");
    (void)has_sender;

    dbus_bool_t has_sig = dbus_message_has_signature(msg, "");
    (void)has_sig;

    dbus_message_unref(msg);
}

// Variant 6: message creation -- new_method_return, new_signal, new_error, new_error_printf, copy
void variant_message_creation() {
    // Create a method call to derive responses from
    DBusMessage *call = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    // Method return
    DBusMessage *ret = dbus_message_new_method_return(call);
    // @msg_alive = 1
    dbus_message_unref(ret);

    // Error response
    DBusMessage *err_msg = dbus_message_new_error(
        call, "com.example.Error.Failed", "Something went wrong");
    // @msg_alive = 1
    dbus_message_unref(err_msg);

    // Formatted error response
    DBusMessage *err_printf = dbus_message_new_error_printf(
        call, "com.example.Error.Code", "Error code: %d", 42);
    // @msg_alive = 1
    dbus_message_unref(err_printf);

    // Signal (standalone creation)
    DBusMessage *signal = dbus_message_new_signal(
        "/com/example/Obj", "com.example.Iface", "Changed");
    // @msg_alive = 1
    dbus_message_unref(signal);

    // Copy
    DBusMessage *copy = dbus_message_copy(call);
    // @msg_alive = 1
    dbus_message_unref(copy);

    dbus_message_unref(call);
}

// Variant 7: ref/unref lifecycle
void variant_message_refcount() {
    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    DBusMessage *ref = dbus_message_ref(msg);
    (void)ref;

    // Two unrefs to match the initial creation + ref
    dbus_message_unref(msg);
    dbus_message_unref(msg);
}

// Variant 8: message argument iteration
void variant_message_args() {
    DBusError err;
    dbus_error_init(&err);

    DBusMessage *msg = dbus_message_new_method_call(
        "com.example.Dest", "/path", "com.example.Iface", "Method");
    // @msg_alive = 1

    // Append arguments
    const char *str_val = "hello";
    (void)dbus_message_append_args(msg,
        DBUS_TYPE_STRING, &str_val,
        DBUS_TYPE_INVALID);

    // Get arguments back
    const char *result;
    (void)dbus_message_get_args(msg, &err,
        DBUS_TYPE_STRING, &result,
        DBUS_TYPE_INVALID);

    // Use iterator-based API for appending
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);

    // Use iterator-based API for reading
    DBusMessageIter read_iter;
    (void)dbus_message_iter_init(msg, &read_iter);

    dbus_message_unref(msg);
    dbus_error_free(&err);
}

int main() {
    variant_method_call_accessors();
    variant_serial_operations();
    variant_auto_start();
    variant_type_queries();
    variant_has_queries();
    variant_message_creation();
    variant_message_refcount();
    variant_message_args();
    return 0;
}
