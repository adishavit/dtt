// 432: Ternary operator with arrays — safe access patterns
// Ternary pointer initialization should track the minimum buffer size
// and safe accesses within that bound should not trigger warnings.

// Variant 1: access within smaller array's bounds
void test_ternary_safe(bool b) {
    int arr1[10];
    int arr2[5];
    int* p = b ? arr1 : arr2;  // min size = 5

    p[4] = 1;                   // Safe: 4 < 5
    p[0] = 2;                   // Safe: 0 < 5
}

// Variant 2: same-size arrays, access within bounds
void test_ternary_same_size_safe(bool b) {
    int arr1[8];
    int arr2[8];
    int* p = b ? arr1 : arr2;  // min size = 8

    p[7] = 1;                   // Safe: 7 < 8
}

// Variant 3: ternary with reversed order
void test_ternary_reversed(bool b) {
    int arr1[3];
    int arr2[10];
    int* p = b ? arr1 : arr2;  // min size = 3

    p[2] = 1;                   // Safe: 2 < 3
}

int main() {
    test_ternary_safe(true);
    test_ternary_same_size_safe(true);
    test_ternary_reversed(true);
    return 0;
}
