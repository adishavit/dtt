// WHAT IS TESTED:
// Buffer aliasing through casts - OOB accesses DTT catches.
// Tests reinterpret_cast, C-style casts, and static_cast through void*.
//
// Expected behavior: FAIL (DTT produces errors for OOB access)
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Array name direct - OOB
// -----------------------------------------------------------------------------
void variant1_array_name_oob() {
    int arr[10];  // 40 bytes
    char* p = reinterpret_cast<char*>(arr);

    p[40] = 'x';  // FAIL: byte 40 beyond 40-byte buffer
}

// -----------------------------------------------------------------------------
// VARIANT 2: 2D array - OOB
// -----------------------------------------------------------------------------
void variant2_2d_array_oob() {
    int matrix[2][5];  // 40 bytes
    char* flat = reinterpret_cast<char*>(matrix);

    flat[40] = 0;  // FAIL: byte 40 beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 3: Struct array - OOB
// -----------------------------------------------------------------------------
struct Point { int x, y; };

void variant3_struct_array_oob() {
    Point points[5];  // 40 bytes
    char* raw = reinterpret_cast<char*>(points);

    raw[40] = 0;  // FAIL: beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 4: void pointer intermediate - OOB
// -----------------------------------------------------------------------------
void variant4_void_ptr_oob() {
    uint64_t data[4];  // 32 bytes
    void* vp = static_cast<void*>(data);
    uint8_t* bp = static_cast<uint8_t*>(vp);

    bp[32] = 0;  // FAIL: beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 5: Chained casts - OOB
// -----------------------------------------------------------------------------
void variant5_chained_casts_oob() {
    int arr[8];  // 32 bytes
    char* p1 = reinterpret_cast<char*>(arr);
    unsigned char* p2 = reinterpret_cast<unsigned char*>(p1);
    uint8_t* p3 = reinterpret_cast<uint8_t*>(p2);

    p3[32] = 0;  // FAIL: beyond buffer
}

// =============================================================================
// C-STYLE CAST VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 6: C-style cast array - OOB
// -----------------------------------------------------------------------------
void variant6_c_style_array_oob() {
    int arr[10];  // 40 bytes
    char* p = (char*)arr;

    p[40] = 'x';  // FAIL: byte 40 beyond 40-byte buffer
}

// -----------------------------------------------------------------------------
// VARIANT 7: C-style cast struct - OOB
// -----------------------------------------------------------------------------
void variant7_c_style_struct_oob() {
    Point pt = {1, 2};  // 8 bytes
    char* p = (char*)&pt;

    p[8] = 0;  // FAIL: beyond struct
}

// -----------------------------------------------------------------------------
// VARIANT 8: C-style cast through void* - OOB
// -----------------------------------------------------------------------------
void variant8_c_style_void_oob() {
    int arr[8];  // 32 bytes
    void* vp = (void*)arr;
    char* p = (char*)vp;

    p[32] = 0;  // FAIL: beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 9: C-style cast chained - OOB
// -----------------------------------------------------------------------------
void variant9_c_style_chained_oob() {
    uint64_t data[4];  // 32 bytes
    unsigned char* uc = (unsigned char*)(void*)data;

    uc[32] = 0xFF;  // FAIL: beyond buffer
}

// =============================================================================
// STATIC_CAST THROUGH VOID* VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 10: static_cast array to void* to char* - OOB
// -----------------------------------------------------------------------------
void variant10_static_void_array_oob() {
    int arr[10];  // 40 bytes
    void* vp = static_cast<void*>(arr);
    char* p = static_cast<char*>(vp);

    p[40] = 'x';  // FAIL: beyond buffer
}

// -----------------------------------------------------------------------------
// VARIANT 11: static_cast struct to void* to char* - OOB
// -----------------------------------------------------------------------------
struct Data { int a, b, c, d; };  // 16 bytes

void variant11_static_void_struct_oob() {
    Data d = {1, 2, 3, 4};
    void* vp = static_cast<void*>(&d);
    unsigned char* bytes = static_cast<unsigned char*>(vp);

    bytes[16] = 0;  // FAIL: beyond struct
}

// -----------------------------------------------------------------------------
// VARIANT 12: static_cast roundtrip - OOB
// -----------------------------------------------------------------------------
void variant12_static_roundtrip_oob() {
    int arr[10];  // 40 bytes
    void* vp = static_cast<void*>(arr);
    int* back = static_cast<int*>(vp);

    back[10] = 11;  // FAIL: index 10 beyond size 10
}

// -----------------------------------------------------------------------------
// VARIANT 13: static_cast double void* - OOB
// -----------------------------------------------------------------------------
void variant13_static_double_void_oob() {
    int arr[8];  // 32 bytes
    void* v1 = static_cast<void*>(arr);
    char* c = static_cast<char*>(v1);
    void* v2 = static_cast<void*>(c);
    uint8_t* uc = static_cast<uint8_t*>(v2);

    uc[32] = 0;  // FAIL: beyond buffer
}

// =============================================================================
// NON-BUFFER POINTER CAST VARIANTS - OOB
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 14: Type punning via char* - OOB
// -----------------------------------------------------------------------------
void variant14_type_pun_char_oob() {
    float f = 3.14159f;  // 4 bytes
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&f);

    bytes[4] = 0xFF;  // FAIL: byte 4 beyond 4-byte float
}

// -----------------------------------------------------------------------------
// VARIANT 15: Struct to char* - OOB
// -----------------------------------------------------------------------------
struct Vec2i { int x, y; };  // 8 bytes

void variant15_struct_char_oob() {
    Vec2i vi = {1, 2};
    char* bytes = reinterpret_cast<char*>(&vi);

    bytes[8] = 0;  // FAIL: beyond struct
}

// -----------------------------------------------------------------------------
// VARIANT 16: Small struct cast - OOB
// -----------------------------------------------------------------------------
struct Small { int a; };  // 4 bytes

void variant16_small_struct_oob() {
    Small s = {42};
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&s);

    bytes[4] = 0;  // FAIL: byte 4 beyond 4-byte struct
}

// -----------------------------------------------------------------------------
// VARIANT 17: Pointer-to-pointer cast - OOB
// -----------------------------------------------------------------------------
void variant17_ptr_ptr_oob() {
    int x = 42;
    int* p = &x;
    int** pp = &p;  // 8 bytes (pointer)

    char* bytes = reinterpret_cast<char*>(pp);

    bytes[sizeof(int*)] = 0;  // FAIL: beyond pointer size
}

// -----------------------------------------------------------------------------
// VARIANT 18: Enum pointer cast - OOB
// -----------------------------------------------------------------------------
enum Color { Red, Green, Blue };

void variant18_enum_ptr_oob() {
    Color c = Green;  // 4 bytes (int-sized enum)
    char* bytes = reinterpret_cast<char*>(&c);

    bytes[sizeof(Color)] = 0;  // FAIL: beyond enum
}

// -----------------------------------------------------------------------------
// VARIANT 19: Union pointer cast - OOB
// -----------------------------------------------------------------------------
void variant19_union_ptr_oob() {
    union { int i; float f; } u;  // 4 bytes
    u.i = 42;

    char* bytes = reinterpret_cast<char*>(&u);

    bytes[4] = 0;  // FAIL: beyond union
}

// -----------------------------------------------------------------------------
// VARIANT 20: Nested struct member cast - OOB
// -----------------------------------------------------------------------------
struct Inner { int val; };  // 4 bytes
struct Wrapper { Inner inner; int extra; };

void variant20_nested_member_oob() {
    Wrapper w = {{42}, 100};

    // Cast address of nested member
    char* bytes = reinterpret_cast<char*>(&w.inner);

    bytes[4] = 0;  // FAIL: beyond Inner (even though Wrapper is larger)
}

// -----------------------------------------------------------------------------
// VARIANT 21: C-style type pun - OOB
// -----------------------------------------------------------------------------
void variant21_c_style_pun_oob() {
    double d = 3.14159;  // 8 bytes
    char* bytes = (char*)&d;

    bytes[8] = 0;  // FAIL: beyond double
}

// -----------------------------------------------------------------------------
// VARIANT 22: static_cast void* single element - OOB
// -----------------------------------------------------------------------------
void variant22_static_single_oob() {
    int x = 42;  // 4 bytes
    void* vp = static_cast<void*>(&x);
    char* bytes = static_cast<char*>(vp);

    bytes[4] = 0;  // FAIL: beyond int
}

int main() {
    // reinterpret_cast variants
    variant1_array_name_oob();
    variant2_2d_array_oob();
    variant3_struct_array_oob();
    variant4_void_ptr_oob();
    variant5_chained_casts_oob();
    // C-style cast variants
    variant6_c_style_array_oob();
    variant7_c_style_struct_oob();
    variant8_c_style_void_oob();
    variant9_c_style_chained_oob();
    // static_cast variants
    variant10_static_void_array_oob();
    variant11_static_void_struct_oob();
    variant12_static_roundtrip_oob();
    variant13_static_double_void_oob();
    // Non-buffer pointer cast variants
    variant14_type_pun_char_oob();
    variant15_struct_char_oob();
    variant16_small_struct_oob();
    variant17_ptr_ptr_oob();
    variant18_enum_ptr_oob();
    variant19_union_ptr_oob();
    variant20_nested_member_oob();
    variant21_c_style_pun_oob();
    variant22_static_single_oob();
    return 0;
}
