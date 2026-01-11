// WHAT IS TESTED:
// Pointer arithmetic on nullptr is UB - should error at arithmetic, not just deref.
//
// Expected: ERROR (on the arithmetic itself)

struct Data { int value; };

// 1. nullptr + constant
void test_null_plus_const() {
    Data* p = nullptr;
    Data* q = p + 5;  // ERROR: nullptr + k is UB
}

// 2. nullptr + variable
void test_null_plus_var(int k) {
    Data* p = nullptr;
    Data* q = p + k;  // ERROR: nullptr + k is UB (unless k==0)
}

// 3. nullptr - constant
void test_null_minus_const() {
    Data* p = nullptr;
    Data* q = p - 3;  // ERROR: nullptr - k is UB
}

// 4. Chained from null
void test_chained_null() {
    Data* p = nullptr;
    Data* q = p + 1;   // ERROR: nullptr + 1 is UB
    Data* r = q + 2;   // Also UB (derived from null)
}

// 5. nullptr + 0 is arguably OK (edge case)
// void test_null_plus_zero() {
//     Data* p = nullptr;
//     Data* q = p + 0;  // Might be OK? Standard is unclear
// }

int main() { return 0; }
