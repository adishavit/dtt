// 995_union_active_fail.cpp - Unsafe union access patterns (C++ mode)
// Tests that DTT catches reading inactive union members (UB in C++)

union IntFloat {
    int i;
    float f;
};

// Variant 1: Reading inactive member after writing different member
void test_inactive_read() {
    IntFloat u;
    u.i = 42;
    float x = u.f;  // FAIL: reading inactive member 'f' when 'i' is active  dtt-error {{inactive union member 'f'}}
    (void)x;
}

// Variant 2: Reading wrong member after reassignment
void test_after_reassign() {
    IntFloat u;
    u.f = 3.14f;  // 'f' is active
    u.i = 42;     // Now 'i' is active
    float x = u.f;  // FAIL: reading inactive 'f' when 'i' is active  dtt-error {{inactive union member 'f'}}
    (void)x;
}

// Variant 3: Default init reads wrong member
void test_default_init() {
    IntFloat u = {42};  // First member 'i' is active
    float x = u.f;  // FAIL: reading inactive 'f' when 'i' is active  dtt-error {{inactive union member 'f'}}
    (void)x;
}

int main() {
    test_inactive_read();
    test_after_reassign();
    test_default_init();
    return 0;
}
