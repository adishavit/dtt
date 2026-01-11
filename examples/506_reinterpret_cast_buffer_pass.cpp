// WHAT IS TESTED:
// Buffer aliasing through casts - valid accesses within bounds.
// Tests reinterpret_cast, C-style casts, and static_cast through void*.
//
// Expected behavior: PASS (all accesses are within bounds)
//
// ============================================================================

#include <cstdint>

// -----------------------------------------------------------------------------
// VARIANT 1: Array name directly (DTT tracks this)
// -----------------------------------------------------------------------------
void variant1_array_name_direct() {
    int arr[10];  // 40 bytes
    char* p = reinterpret_cast<char*>(arr);

    p[0] = 'a';
    p[39] = 'z';  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 2: 2D array (DTT tracks this)
// -----------------------------------------------------------------------------
void variant2_2d_array() {
    int matrix[2][5];  // 40 bytes
    char* flat = reinterpret_cast<char*>(matrix);

    flat[0] = 0;
    flat[39] = 0;  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 3: Array of structs (DTT tracks this)
// -----------------------------------------------------------------------------
struct Point { int x, y; };

void variant3_struct_array() {
    Point points[5];  // 40 bytes
    char* raw = reinterpret_cast<char*>(points);

    raw[0] = 0;
    raw[39] = 0;  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 4: void pointer intermediate (DTT tracks this)
// -----------------------------------------------------------------------------
void variant4_void_ptr() {
    uint64_t data[4];  // 32 bytes
    void* vp = static_cast<void*>(data);
    uint8_t* bp = static_cast<uint8_t*>(vp);

    bp[0] = 0;
    bp[31] = 0;  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 5: Chained casts (DTT tracks this)
// -----------------------------------------------------------------------------
void variant5_chained_casts() {
    int arr[8];  // 32 bytes
    char* p1 = reinterpret_cast<char*>(arr);
    unsigned char* p2 = reinterpret_cast<unsigned char*>(p1);
    uint8_t* p3 = reinterpret_cast<uint8_t*>(p2);

    p3[0] = 0;
    p3[31] = 0;  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 6: Single element cast
// -----------------------------------------------------------------------------
void variant6_single_element() {
    int x = 0x12345678;
    char* p = reinterpret_cast<char*>(&x);

    p[0] = 0x11;
    p[3] = 0x44;  // Last valid byte of int
}

// =============================================================================
// C-STYLE CAST VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 7: C-style cast array name
// -----------------------------------------------------------------------------
void variant7_c_style_array() {
    int arr[10];  // 40 bytes
    char* p = (char*)arr;

    p[0] = 'a';
    p[39] = 'z';  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 8: C-style cast struct
// -----------------------------------------------------------------------------
void variant8_c_style_struct() {
    Point pt = {1, 2};  // 8 bytes
    char* p = (char*)&pt;

    p[0] = 0;
    p[7] = 0;  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 9: C-style cast through void*
// -----------------------------------------------------------------------------
void variant9_c_style_void() {
    int arr[8];  // 32 bytes
    void* vp = (void*)arr;
    char* p = (char*)vp;

    p[0] = 0;
    p[31] = 0;  // Last valid byte
}

// -----------------------------------------------------------------------------
// VARIANT 10: C-style cast chained
// -----------------------------------------------------------------------------
void variant10_c_style_chained() {
    uint64_t data[4];  // 32 bytes
    unsigned char* uc = (unsigned char*)(void*)data;

    uc[0] = 0xFF;
    uc[31] = 0xFF;
}

// =============================================================================
// STATIC_CAST THROUGH VOID* VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 11: static_cast array to void* to char*
// -----------------------------------------------------------------------------
void variant11_static_void_array() {
    int arr[10];  // 40 bytes
    void* vp = static_cast<void*>(arr);
    char* p = static_cast<char*>(vp);

    p[0] = 'a';
    p[39] = 'z';
}

// -----------------------------------------------------------------------------
// VARIANT 12: static_cast struct to void* to char*
// -----------------------------------------------------------------------------
void variant12_static_void_struct() {
    Point pt = {1, 2};  // 8 bytes
    void* vp = static_cast<void*>(&pt);
    unsigned char* bytes = static_cast<unsigned char*>(vp);

    bytes[0] = 0;
    bytes[7] = 0;
}

// -----------------------------------------------------------------------------
// VARIANT 13: static_cast roundtrip through void*
// -----------------------------------------------------------------------------
void variant13_static_roundtrip() {
    int arr[10];  // 40 bytes
    void* vp = static_cast<void*>(arr);
    int* back = static_cast<int*>(vp);

    back[0] = 1;
    back[9] = 10;  // Last valid element
}

// -----------------------------------------------------------------------------
// VARIANT 14: static_cast double void* conversion
// -----------------------------------------------------------------------------
void variant14_static_double_void() {
    int arr[8];  // 32 bytes
    void* v1 = static_cast<void*>(arr);
    char* c = static_cast<char*>(v1);
    void* v2 = static_cast<void*>(c);
    uint8_t* uc = static_cast<uint8_t*>(v2);

    uc[0] = 0;
    uc[31] = 0;
}

// =============================================================================
// NON-BUFFER POINTER CAST VARIANTS
// =============================================================================

// -----------------------------------------------------------------------------
// VARIANT 15: Type punning int to float (same size)
// -----------------------------------------------------------------------------
void variant15_type_pun_int_float() {
    int i = 0x40490FDB;  // float bits for ~3.14159
    float* fp = reinterpret_cast<float*>(&i);

    (void)*fp;  // Read is valid - same size
}

// -----------------------------------------------------------------------------
// VARIANT 16: Type punning through char* (standard-compliant)
// -----------------------------------------------------------------------------
void variant16_type_pun_via_char() {
    float f = 3.14159f;
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&f);

    // Access all 4 bytes - valid
    unsigned char b0 = bytes[0];
    unsigned char b1 = bytes[1];
    unsigned char b2 = bytes[2];
    unsigned char b3 = bytes[3];
    (void)b0; (void)b1; (void)b2; (void)b3;
}

// -----------------------------------------------------------------------------
// VARIANT 17: Struct to different struct (same size)
// -----------------------------------------------------------------------------
struct Vec2i { int x, y; };
struct Vec2f { float x, y; };

void variant17_struct_to_struct() {
    Vec2i vi = {1, 2};
    // Reinterpret as float pair (both 8 bytes)
    Vec2f* vf = reinterpret_cast<Vec2f*>(&vi);

    (void)vf->x;
    (void)vf->y;
}

// -----------------------------------------------------------------------------
// VARIANT 18: Pointer to integer roundtrip
// -----------------------------------------------------------------------------
void variant18_ptr_to_int() {
    int arr[10];
    uintptr_t addr = reinterpret_cast<uintptr_t>(arr);
    int* back = reinterpret_cast<int*>(addr);

    back[0] = 42;
    back[9] = 99;  // Still valid
}

// -----------------------------------------------------------------------------
// VARIANT 19: Const cast away and access
// -----------------------------------------------------------------------------
void variant19_const_cast() {
    int x = 42;
    const int* cp = &x;
    int* p = const_cast<int*>(cp);

    if (p) *p = 100;  // Valid - original wasn't const
}

// -----------------------------------------------------------------------------
// VARIANT 20: Volatile pointer cast
// -----------------------------------------------------------------------------
void variant20_volatile_cast() {
    int x = 42;
    volatile int* vp = &x;
    int* p = const_cast<int*>(vp);

    if (p) *p = 100;  // Valid access
}

// -----------------------------------------------------------------------------
// VARIANT 21: Cast to larger struct, access original size only
// -----------------------------------------------------------------------------
struct Small { int a; };           // 4 bytes
struct Large { int a, b, c, d; };  // 16 bytes

void variant21_small_to_large_partial() {
    Small s = {42};
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&s);

    // Only access within Small's size
    bytes[0] = 0;
    bytes[3] = 0;  // Last valid byte of Small
}

// -----------------------------------------------------------------------------
// VARIANT 22: Union-style type punning via pointer
// -----------------------------------------------------------------------------
void variant22_union_style() {
    union { int i; float f; } u;
    u.i = 0x40490FDB;

    int* ip = &u.i;
    float* fp = reinterpret_cast<float*>(ip);

    if (fp) (void)*fp;  // Valid - union semantics
}

// -----------------------------------------------------------------------------
// VARIANT 23: Pointer to pointer cast
// -----------------------------------------------------------------------------
void variant23_ptr_to_ptr() {
    int x = 42;
    int* p = &x;
    int** pp = &p;

    // Cast pointer-to-pointer to char*
    char* bytes = reinterpret_cast<char*>(pp);

    // Access within pointer size (8 bytes on 64-bit)
    bytes[0] = bytes[0];
    bytes[sizeof(int*) - 1] = bytes[sizeof(int*) - 1];
}

// -----------------------------------------------------------------------------
// VARIANT 24: Function pointer storage (cast to void*)
// -----------------------------------------------------------------------------
void dummy_func() {}

void variant24_func_ptr_storage() {
    void (*fp)() = dummy_func;

    // Store function pointer as void* (common pattern)
    void* storage = reinterpret_cast<void*>(fp);

    // Restore it
    void (*restored)() = reinterpret_cast<void(*)()>(storage);
    restored();
}

// -----------------------------------------------------------------------------
// VARIANT 25: Enum to int pointer cast
// -----------------------------------------------------------------------------
enum Color { Red, Green, Blue };

void variant25_enum_ptr() {
    Color c = Green;
    int* ip = reinterpret_cast<int*>(&c);

    *ip = Blue;  // Valid - enum has int underlying type
}

// -----------------------------------------------------------------------------
// VARIANT 26: C-style cast for type punning
// -----------------------------------------------------------------------------
void variant26_c_style_pun() {
    double d = 3.14159;
    uint64_t* bits = (uint64_t*)&d;

    (void)*bits;  // Valid - same size
}

// -----------------------------------------------------------------------------
// VARIANT 27: Nested struct member cast
// -----------------------------------------------------------------------------
struct Inner { int val; };
struct Wrapper { Inner inner; int extra; };

void variant27_nested_member() {
    Wrapper w = {{42}, 100};

    // Cast address of nested member
    char* bytes = reinterpret_cast<char*>(&w.inner);

    bytes[0] = 0;
    bytes[3] = 0;  // Last byte of Inner
}

int main() {
    // reinterpret_cast variants
    variant1_array_name_direct();
    variant2_2d_array();
    variant3_struct_array();
    variant4_void_ptr();
    variant5_chained_casts();
    variant6_single_element();
    // C-style cast variants
    variant7_c_style_array();
    variant8_c_style_struct();
    variant9_c_style_void();
    variant10_c_style_chained();
    // static_cast variants
    variant11_static_void_array();
    variant12_static_void_struct();
    variant13_static_roundtrip();
    variant14_static_double_void();
    // Non-buffer pointer cast variants
    variant15_type_pun_int_float();
    variant16_type_pun_via_char();
    variant17_struct_to_struct();
    variant18_ptr_to_int();
    variant19_const_cast();
    variant20_volatile_cast();
    variant21_small_to_large_partial();
    variant22_union_style();
    variant23_ptr_to_ptr();
    variant24_func_ptr_storage();
    variant25_enum_ptr();
    variant26_c_style_pun();
    variant27_nested_member();
    return 0;
}
