// libdbus signature validation and type queries (should COMPILE)
//
// Exercises dbus_signature_validate, dbus_signature_validate_single,
// dbus_type_is_valid, dbus_type_is_basic, dbus_type_is_container,
// and dbus_type_is_fixed. These functions have minimal preconditions
// (only _nonnull on signature strings).

#include <dbus/dbus.h>

// Variant 1: signature validation
void variant_signature_validate() {
    DBusError err;
    dbus_error_init(&err);

    dbus_bool_t valid = dbus_signature_validate("si", &err);
    (void)valid;

    dbus_bool_t single = dbus_signature_validate_single("s", &err);
    (void)single;

    dbus_error_free(&err);
}

// Variant 2: type classification queries
void variant_type_queries() {
    dbus_bool_t is_valid = dbus_type_is_valid(DBUS_TYPE_STRING);
    (void)is_valid;

    dbus_bool_t is_basic = dbus_type_is_basic(DBUS_TYPE_INT32);
    (void)is_basic;

    dbus_bool_t is_container = dbus_type_is_container(DBUS_TYPE_ARRAY);
    (void)is_container;

    dbus_bool_t is_fixed = dbus_type_is_fixed(DBUS_TYPE_DOUBLE);
    (void)is_fixed;
}

// Variant 3: combined -- validate a complex signature and check types
void variant_combined() {
    DBusError err;
    dbus_error_init(&err);

    dbus_bool_t v1 = dbus_signature_validate("a{sv}", &err);
    (void)v1;

    dbus_bool_t v2 = dbus_signature_validate_single("a{sv}", &err);
    (void)v2;

    // Check that ARRAY is a container and STRING is basic
    (void)dbus_type_is_container(DBUS_TYPE_ARRAY);
    (void)dbus_type_is_basic(DBUS_TYPE_STRING);
    (void)dbus_type_is_fixed(DBUS_TYPE_UINT32);
    (void)dbus_type_is_valid(DBUS_TYPE_VARIANT);

    dbus_error_free(&err);
}

int main() {
    variant_signature_validate();
    variant_type_queries();
    variant_combined();
    return 0;
}
