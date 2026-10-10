// WHAT IS TESTED:
// Pointer arithmetic on nullptr is UB - should error at arithmetic, not just deref.
//
// Expected: ERROR (on the arithmetic itself)

struct Data { int value; };

// 1. nullptr + constant
void test_null_plus_const() {
    Data* p = nullptr;
    Data* q = p + 5;  // ERROR: nullptr + k is UB  dtt-error {{pointer arithmetic on null pointer}}
}

// 2. nullptr + variable
void test_null_plus_var(int k) {
    Data* p = nullptr;
    Data* q = p + k;  // ERROR: nullptr + k is UB (unless k==0)  dtt-error {{pointer arithmetic on null pointer}}
}

// 3. nullptr - constant
void test_null_minus_const() {
    Data* p = nullptr;
    Data* q = p - 3;  // ERROR: nullptr - k is UB  dtt-error {{pointer arithmetic on null pointer}}
}

// 4. Chained from null
void test_chained_null() {
    Data* p = nullptr;
    Data* q = p + 1;   // ERROR: nullptr + 1 is UB  dtt-error {{pointer arithmetic on null pointer}}
    // Data* r = q + 2;   // Would also be UB but q already errored
}

// 5. Prefix increment
void test_null_prefix_inc() {
    Data* p = nullptr;
    ++p;  // ERROR: incrementing null pointer is UB  dtt-error {{pointer arithmetic on null pointer}}
}

// 6. Postfix increment
void test_null_postfix_inc() {
    Data* p = nullptr;
    p++;  // ERROR: incrementing null pointer is UB  dtt-error {{pointer arithmetic on null pointer}}
}

// 7. Prefix decrement
void test_null_prefix_dec() {
    Data* p = nullptr;
    --p;  // ERROR: decrementing null pointer is UB  dtt-error {{pointer arithmetic on null pointer}}
}

// 8. Compound assignment
void test_null_compound_assign() {
    Data* p = nullptr;
    p += 3;  // ERROR: null pointer arithmetic is UB  dtt-error {{pointer arithmetic on null pointer}}
}

// Note: p[5] is *(p+5), tested via 137_nullptr_pointer_arithmetic_fail.cpp

int main() { return 0; }
