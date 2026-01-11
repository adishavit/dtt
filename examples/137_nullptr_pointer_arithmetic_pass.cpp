// WHAT IS TESTED:
// Pointer arithmetic should preserve nullability tracking.
//
// Expected: PASS

struct Data { int value; };

// 1. Array base + offset
void test_array_offset() {
    Data arr[10];
    Data* ptr = arr;
    Data* ptr2 = ptr + 5;
    ptr2->value = 42;  // Safe: base is non-null array
}

// 2. Inline arithmetic expression
void test_inline_arithmetic() {
    Data arr[10];
    Data* ptr = arr;
    (ptr + 3)->value = 10;  // Safe: inline arithmetic from array
}

// 3. Chained pointer arithmetic
void test_chained_arithmetic() {
    Data arr[10];
    Data* p1 = arr;
    Data* p2 = p1 + 2;
    Data* p3 = p2 + 3;  // p3 = arr + 5
    p3->value = 1;      // Safe: chain preserves non-null
}

// 4. Subtraction within bounds
void test_subtraction() {
    Data arr[10];
    Data* end = arr + 10;
    Data* ptr = end - 5;  // ptr = arr + 5
    ptr->value = 1;       // Safe: within bounds
}

// 5. Zero offset
void test_zero_offset() {
    Data arr[10];
    Data* ptr = arr + 0;
    ptr->value = 1;  // Safe: same as arr
}

int main() {
    test_array_offset();
    test_inline_arithmetic();
    test_chained_arithmetic();
    test_subtraction();
    test_zero_offset();
    return 0;
}
