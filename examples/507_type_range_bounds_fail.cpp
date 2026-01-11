// WHAT IS TESTED:
// Type range bounds - OOB cases DTT catches (constant indices).
//
// Expected behavior: FAIL (DTT produces errors)
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Constant index OOB
// -----------------------------------------------------------------------------
void variant1_const_oob() {
    int arr[256];

    arr[256] = 1;  // FAIL: index 256 OOB for size 256
}

// -----------------------------------------------------------------------------
// VARIANT 2: Constant uchar value OOB
// -----------------------------------------------------------------------------
void variant2_const_uchar_oob() {
    int table[100];
    unsigned char idx = 150;  // Constant tracked by DTT

    table[idx] = 42;  // FAIL: DTT knows idx=150 >= 100
}

// -----------------------------------------------------------------------------
// VARIANT 3: Constant after arithmetic
// -----------------------------------------------------------------------------
void variant3_arithmetic_oob() {
    int data[10];
    int base = 5;
    int offset = 6;

    data[base + offset] = 1;  // FAIL: 5+6=11 >= 10
}

// -----------------------------------------------------------------------------
// VARIANT 4: Loop exceeds bounds
// -----------------------------------------------------------------------------
void variant4_loop_oob() {
    int arr[10];

    for (int i = 0; i <= 10; ++i) {  // Off-by-one: <= instead of <
        arr[i] = i;  // FAIL: i=10 is OOB
    }
}

// -----------------------------------------------------------------------------
// VARIANT 5: Negative index
// -----------------------------------------------------------------------------
void variant5_negative() {
    int arr[10];
    int idx = -1;

    arr[idx] = 0;  // FAIL: negative index
}

int main() {
    variant1_const_oob();
    variant2_const_uchar_oob();
    variant3_arithmetic_oob();
    variant4_loop_oob();
    variant5_negative();
    return 0;
}
