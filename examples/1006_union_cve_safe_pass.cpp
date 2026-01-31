// 1006_union_cve_safe_pass.cpp - Safe tagged union patterns
// Demonstrates correct discriminant checking that DTT should allow

// COM VARIANT-style tagged union
enum VariantType { VT_INT = 0, VT_FLOAT = 1, VT_BSTR = 8, VT_UNKNOWN = 13 };
union VariantData {
    int intVal;
    float fltVal;
    char* bstrVal;
    void* punkVal;
};
struct Variant {
    VariantType vt;
    VariantData data;
};

// Variant 1: Proper discriminant check before access
void test_proper_check(Variant* var) {
    if (var->vt == VT_INT) {
        int x = var->data.intVal;  // OK - checked vt first
        (void)x;
    } else if (var->vt == VT_BSTR) {
        char* s = var->data.bstrVal;  // OK - checked vt first
        (void)s;
    }
}

// Variant 2: Switch statement with all cases
int test_switch_all_cases(Variant* var) {
    switch (var->vt) {
    case VT_INT:
        return var->data.intVal;  // OK
    case VT_FLOAT:
        return (int)var->data.fltVal;  // OK
    case VT_BSTR:
        return var->data.bstrVal ? 1 : 0;  // OK
    case VT_UNKNOWN:
        return var->data.punkVal ? 1 : 0;  // OK
    default:
        return -1;
    }
}

// Variant 3: Initialization then immediate use
void test_init_then_use() {
    Variant v;
    v.vt = VT_INT;
    v.data.intVal = 42;
    int x = v.data.intVal;  // OK - just set vt to VT_INT
    (void)x;
}

// Variant 4: Write active member
void test_write_active(Variant* var) {
    var->vt = VT_FLOAT;
    var->data.fltVal = 3.14f;  // OK - setting newly active member
}

// Variant 5: Read immediately after write
void test_read_after_write() {
    Variant v;
    v.vt = VT_BSTR;
    v.data.bstrVal = (char*)"hello";
    char* s = v.data.bstrVal;  // OK - reading what we just wrote
    (void)s;
}

// Network message with proper validation
enum MsgType { MSG_REQUEST = 1, MSG_RESPONSE = 2 };
union MsgPayload {
    struct { int methodId; } request;
    struct { int resultCode; } response;
};
struct Message {
    MsgType type;
    MsgPayload payload;
};

// Variant 6: Validate before processing
void test_validate_message(Message* msg) {
    // Proper validation
    if (msg->type != MSG_REQUEST && msg->type != MSG_RESPONSE) {
        return;  // Reject invalid
    }
    if (msg->type == MSG_REQUEST) {
        int method = msg->payload.request.methodId;  // OK
        (void)method;
    }
}

// Variant 7: Helper function with type parameter
int get_variant_int(Variant* var) {
    if (var->vt != VT_INT) {
        return 0;  // Error case
    }
    return var->data.intVal;  // OK - checked above
}

// Variant 8: Type-safe wrapper
struct SafeVariant {
    VariantType vt;
    VariantData data;

    int getInt() const {
        if (vt == VT_INT) {
            return data.intVal;  // OK
        }
        return 0;
    }
    float getFloat() const {
        if (vt == VT_FLOAT) {
            return data.fltVal;  // OK
        }
        return 0.0f;
    }
};

void test_safe_wrapper() {
    SafeVariant sv;
    sv.vt = VT_INT;
    sv.data.intVal = 100;
    int x = sv.getInt();  // OK - uses safe accessor
    (void)x;
}

// Common initial sequence (allowed in C++)
union CommonInitial {
    struct { int tag; int value1; } a;
    struct { int tag; float value2; } b;
};

// Variant 9: Common initial sequence access
void test_common_initial_sequence() {
    CommonInitial u;
    u.a.tag = 1;
    u.a.value1 = 42;
    int tag = u.b.tag;  // OK - common initial sequence (same type at same offset)
    (void)tag;
}

// Variant 10: Char access for serialization (always allowed)
void test_char_access() {
    Variant v;
    v.vt = VT_INT;
    v.data.intVal = 0x12345678;
    unsigned char* bytes = (unsigned char*)&v.data;
    unsigned char b = bytes[0];  // OK - char access for serialization
    (void)b;
}

int main() {
    Variant v;
    v.vt = VT_INT;
    v.data.intVal = 0;

    test_proper_check(&v);
    test_switch_all_cases(&v);
    test_init_then_use();
    test_write_active(&v);
    test_read_after_write();

    Message msg;
    msg.type = MSG_REQUEST;
    test_validate_message(&msg);
    get_variant_int(&v);
    test_safe_wrapper();
    test_common_initial_sequence();
    test_char_access();

    return 0;
}
