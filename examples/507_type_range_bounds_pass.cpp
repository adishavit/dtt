// WHAT IS TESTED:
// Type range bounds - cases DTT correctly handles.
//
// Expected behavior: PASS (all accesses are within bounds)
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Local uchar variable with constant init
// -----------------------------------------------------------------------------
void variant1_local_uchar() {
    int table[256];
    unsigned char idx = 100;  // Constant, DTT tracks value

    table[idx] = 42;  // PASS: DTT knows idx=100
}

// -----------------------------------------------------------------------------
// VARIANT 2: Local ushort variable with constant init
// -----------------------------------------------------------------------------
void variant2_local_ushort() {
    static unsigned short lookup[65536];
    unsigned short h = 1000;

    lookup[h] = 0;  // PASS: DTT knows h=1000
}

// -----------------------------------------------------------------------------
// VARIANT 3: Constant indices
// -----------------------------------------------------------------------------
void variant3_const_index() {
    int arr[256];

    arr[0] = 1;
    arr[127] = 2;
    arr[255] = 3;  // PASS: constant indices
}

// -----------------------------------------------------------------------------
// VARIANT 4: Loop with tracked bound
// -----------------------------------------------------------------------------
void variant4_bounded_loop() {
    int data[256];

    for (int i = 0; i < 100; ++i) {
        data[i] = i;  // PASS: loop bound tracked
    }
}

// -----------------------------------------------------------------------------
// VARIANT 5: Array larger than type range
// -----------------------------------------------------------------------------
void variant5_larger_array() {
    int table[300];  // Larger than uchar max
    unsigned char idx = 200;

    table[idx] = 42;  // PASS: 200 < 300
}

// -----------------------------------------------------------------------------
// VARIANT 6: uint8_t local with constant
// -----------------------------------------------------------------------------
void variant6_uint8() {
    char mapping[256];
    uint8_t byte = 0xFF;

    mapping[byte] = 'x';  // PASS: DTT knows byte=255
}

int main() {
    variant1_local_uchar();
    variant2_local_ushort();
    variant3_const_index();
    variant4_bounded_loop();
    variant5_larger_array();
    variant6_uint8();
    return 0;
}
