// 994_union_active_pass.cpp - Safe union access patterns (C++ mode)
// Tests that DTT allows reading active union members

union IntFloat {
    int i;
    float f;
};

union WithArray {
    int arr_i[4];
    float arr_f[4];
};

// Variant 1: Basic correct access after write
void test_basic_active() {
    IntFloat u;
    u.i = 42;
    int x = u.i;  // OK - reading active member
    (void)x;
}

// Variant 2: Re-assignment changes active member
void test_reassign() {
    IntFloat u;
    u.i = 42;
    int x = u.i;  // OK

    u.f = 3.14f;  // Now 'f' is active
    float y = u.f;  // OK - reading newly active member
    (void)x;
    (void)y;
}

// Variant 3: Designated initializer (C++20)
void test_designated_init() {
    IntFloat u = {.f = 3.14f};
    float x = u.f;  // OK - 'f' is active via designated init
    (void)x;
}

// Variant 4: Brace init (first member)
void test_brace_init() {
    IntFloat u = {42};  // First member 'i' is active
    int x = u.i;  // OK
    (void)x;
}

// Variant 5: Array member access
void test_array_member() {
    WithArray u;
    u.arr_i[0] = 1;
    u.arr_i[1] = 2;
    int x = u.arr_i[0];  // OK - arr_i is active
    int y = u.arr_i[1];  // OK
    (void)x;
    (void)y;
}

// Variant 6: Taking address of inactive member is OK
void test_address_inactive() {
    IntFloat u;
    u.i = 42;
    float *pf = &u.f;  // OK - just taking address, not reading
    (void)pf;
}

// Variant 7: Char access is always allowed (for serialization)
void test_char_access() {
    IntFloat u;
    u.i = 0x12345678;
    char *bytes = reinterpret_cast<char*>(&u);
    char c = bytes[0];  // OK - char access always allowed
    (void)c;
}

// Variant 8: Common initial sequence (both structs start with int)
union CommonInitial {
    struct { int tag; int data; } a;
    struct { int tag; float data; } b;
};

void test_common_initial_sequence() {
    CommonInitial u;
    u.a = {1, 42};
    int tag = u.b.tag;  // OK - common initial sequence
    (void)tag;
}

int main() {
    test_basic_active();
    test_reassign();
    test_designated_init();
    test_brace_init();
    test_array_member();
    test_address_inactive();
    test_char_access();
    test_common_initial_sequence();
    return 0;
}
