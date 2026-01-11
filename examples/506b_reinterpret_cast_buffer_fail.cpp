// WHAT IS TESTED:
// Buffer aliasing with &arr[0] and &struct patterns - OOB accesses.
// Tests reinterpret_cast, C-style casts, and static_cast through void*.
//
// Expected behavior: FAIL (DTT catches OOB access)
//
// ORIGIN: Found in tinyexr.h:9320 - unsigned int[3] cast to unsigned char*
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: &arr[0] pattern (the tinyexr case)
// -----------------------------------------------------------------------------
void variant1_array_element_ref() {
    unsigned int datai[3] = {0, 0, 0};  // 12 bytes total
    unsigned char* data = reinterpret_cast<unsigned char*>(&datai[0]);

    data[12] = 0xFF;  // FAIL: byte 12 is beyond 12-byte buffer
}

// -----------------------------------------------------------------------------
// VARIANT 2: Struct to char pointer
// -----------------------------------------------------------------------------
struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t size;
};

void variant2_struct_to_char_ptr() {
    Header h = {0x12345678, 1, 0, 100};
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&h);

    bytes[sizeof(Header)] = 0;  // FAIL: one past end of struct
}

// -----------------------------------------------------------------------------
// VARIANT 3: Nested struct
// -----------------------------------------------------------------------------
struct Outer {
    int prefix;
    Header header;
    int suffix;
};

void variant3_nested_struct() {
    Outer o = {};
    char* p = reinterpret_cast<char*>(&o);

    p[sizeof(Outer)] = 0;  // FAIL: beyond struct
}

// -----------------------------------------------------------------------------
// VARIANT 4: Array element ref with offset
// -----------------------------------------------------------------------------
void variant4_element_ref_offset() {
    int arr[10];  // 40 bytes
    char* p = reinterpret_cast<char*>(&arr[2]);  // Points to byte 8

    // From byte 8, there are 32 bytes remaining (bytes 8-39)
    p[32] = 'x';  // FAIL: byte 40 total, beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 5: Single element OOB (DTT loses size through &x)
// -----------------------------------------------------------------------------
void variant5_single_element_oob() {
    int x = 0x12345678;
    char* p = reinterpret_cast<char*>(&x);

    p[4] = 0xFF;  // FAIL: byte 4 beyond 4-byte int
}

// =============================================================================
// C-STYLE CAST VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 6: C-style cast &arr[0] pattern
// -----------------------------------------------------------------------------
void variant6_c_style_array_elem() {
    unsigned int datai[3] = {0, 0, 0};  // 12 bytes total
    unsigned char* data = (unsigned char*)&datai[0];

    data[12] = 0xFF;  // FAIL: byte 12 beyond 12-byte buffer
}

// -----------------------------------------------------------------------------
// VARIANT 7: C-style cast struct address
// -----------------------------------------------------------------------------
void variant7_c_style_struct() {
    Header h = {0x12345678, 1, 0, 100};
    unsigned char* bytes = (unsigned char*)&h;

    bytes[sizeof(Header)] = 0;  // FAIL: one past end of struct
}

// -----------------------------------------------------------------------------
// VARIANT 8: C-style cast nested struct
// -----------------------------------------------------------------------------
void variant8_c_style_nested() {
    Outer o = {};
    char* p = (char*)&o;

    p[sizeof(Outer)] = 0;  // FAIL: beyond struct
}

// -----------------------------------------------------------------------------
// VARIANT 9: C-style cast element ref with offset
// -----------------------------------------------------------------------------
void variant9_c_style_offset() {
    int arr[10];  // 40 bytes
    char* p = (char*)&arr[2];  // Points to byte 8

    p[32] = 'x';  // FAIL: byte 40 total, beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 10: C-style cast single element
// -----------------------------------------------------------------------------
void variant10_c_style_single() {
    int x = 0x12345678;
    char* p = (char*)&x;

    p[4] = 0xFF;  // FAIL: byte 4 beyond 4-byte int
}

// =============================================================================
// STATIC_CAST THROUGH VOID* VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 11: static_cast &arr[0] through void*
// -----------------------------------------------------------------------------
void variant11_static_array_elem() {
    unsigned int datai[3] = {0, 0, 0};  // 12 bytes total
    void* vp = static_cast<void*>(&datai[0]);
    unsigned char* data = static_cast<unsigned char*>(vp);

    data[12] = 0xFF;  // FAIL: beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 12: static_cast struct address through void*
// -----------------------------------------------------------------------------
void variant12_static_struct() {
    Header h = {0x12345678, 1, 0, 100};
    void* vp = static_cast<void*>(&h);
    unsigned char* bytes = static_cast<unsigned char*>(vp);

    bytes[sizeof(Header)] = 0;  // FAIL: beyond struct
}

// -----------------------------------------------------------------------------
// VARIANT 13: static_cast single element through void*
// -----------------------------------------------------------------------------
void variant13_static_single() {
    int x = 0x12345678;
    void* vp = static_cast<void*>(&x);
    char* p = static_cast<char*>(vp);

    p[4] = 0xFF;  // FAIL: byte 4 beyond 4-byte int
}

int main() {
    // reinterpret_cast variants
    variant1_array_element_ref();
    variant2_struct_to_char_ptr();
    variant3_nested_struct();
    variant4_element_ref_offset();
    variant5_single_element_oob();
    // C-style cast variants
    variant6_c_style_array_elem();
    variant7_c_style_struct();
    variant8_c_style_nested();
    variant9_c_style_offset();
    variant10_c_style_single();
    // static_cast variants
    variant11_static_array_elem();
    variant12_static_struct();
    variant13_static_single();
    return 0;
}
