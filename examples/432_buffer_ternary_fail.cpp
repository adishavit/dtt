// 432: Ternary operator with arrays — buffer overflow detection
// Ternary pointer initialization should track the minimum buffer size
// from both branches so out-of-bounds access is caught.

// Variant 1: access beyond smaller array's bounds
void test_ternary_oob(bool b) {
    int arr1[10];
    int arr2[5];
    int* p = b ? arr1 : arr2;  // min size = 5

    p[6] = 1;                   // FAIL: 6 >= 5 (arr2 too small)
}

// Variant 2: access at exact boundary of smaller array
void test_ternary_boundary(bool b) {
    int arr1[10];
    int arr2[5];
    int* p = b ? arr1 : arr2;  // min size = 5

    p[5] = 1;                   // FAIL: 5 >= 5 (off-by-one)
}

// Variant 3: same-size arrays, access beyond both
void test_ternary_same_size(bool b) {
    int arr1[8];
    int arr2[8];
    int* p = b ? arr1 : arr2;  // min size = 8

    p[8] = 1;                   // FAIL: 8 >= 8 (off-by-one)
}

int main() {
    test_ternary_oob(true);
    test_ternary_boundary(true);
    test_ternary_same_size(true);
    return 0;
}
