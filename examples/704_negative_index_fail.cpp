// 704_negative_index_xfail.cpp
// Gap: DTT doesn't flag negative array indexing
// Some negative indices are bugs, some are intentional patterns
// DTT should at least warn about suspicious negative indexing

#include <cstdlib>
#include <cstdint>
#include <cstring>

// Variant 1: Simple negative index - clear bug
void variant1_simple_negative() {
    int arr[10] = {0};
    int x = arr[-1];  // BUG: negative index underflow  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 2: Negative index on heap array
void variant2_heap_negative() {
    int* arr = (int*)malloc(10 * sizeof(int));
    if (!arr) return;

    arr[-1] = 42;  // BUG: writes before allocation  dtt-error {{out of bounds}}
    free(arr);
}

// Variant 3: Negative index from signed arithmetic
void variant3_signed_arithmetic_negative() {
    int arr[10] = {0};
    int i = 5;
    int j = 10;
    int x = arr[i - j];  // BUG: i-j = -5, negative index  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 4: Negative index from loop underflow
void variant4_loop_underflow() {
    int arr[10] = {0};

    for (int i = 9; i >= 0; i--) {
        arr[i] = i;  // OK while i >= 0
    }
    // What if loop condition is wrong?
    for (int i = 9; i > -5; i--) {  // Loop goes negative
        arr[i] = i;  // KNOWN GAP (not asserted):when i < 0
    }
}

// Variant 5: Negative index through pointer arithmetic
void variant5_pointer_negative() {
    int arr[10] = {0};
    int* p = arr + 5;
    int* q = p - 10;  // q now points before arr
    *q = 42;          // BUG: dereference before array  dtt-error {{out of bounds}}
}

// Variant 6: Intentional negative index pattern (like SDS)
// This is a VALID pattern used by some data structures
struct MetadataHeader {
    uint8_t type;
    uint8_t flags;
};

void variant6_intentional_negative_safe() {
    // Allocate header + data together
    char* buf = (char*)malloc(sizeof(MetadataHeader) + 100);
    if (!buf) return;

    MetadataHeader* hdr = (MetadataHeader*)buf;
    hdr->type = 1;
    hdr->flags = 0;

    char* data = buf + sizeof(MetadataHeader);

    // Access header via negative index from data pointer - INTENTIONAL
    // This is the SDS-style pattern
    uint8_t type = ((uint8_t*)data)[-2];  // Accessing header from data pointer
    (void)type;

    free(buf);
}

// Variant 7: Negative index BUG in metadata pattern
void variant7_intentional_negative_bug() {
    // Oops - forgot to allocate the header!
    char* data = (char*)malloc(100);  // No header space
    if (!data) return;

    // Trying to access "header" that doesn't exist
    uint8_t type = ((uint8_t*)data)[-2];  // KNOWN GAP (not asserted): no header was allocated
    (void)type;

    free(data);
}

// Variant 8: Negative index from size_t underflow
void variant8_size_t_underflow() {
    int arr[10] = {0};
    size_t i = 0;
    size_t idx = i - 1;  // Underflow: idx is now SIZE_MAX
    // As array index, this is a HUGE positive number, not negative
    // But semantically it's the same bug
    int x = arr[idx];  // BUG: massive out of bounds  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 9: Negative index from cast
void variant9_cast_negative() {
    int arr[10] = {0};
    unsigned int u = 5;
    int s = -10;
    int x = arr[(int)u + s];  // BUG: 5 + (-10) = -5  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 10: Ring buffer wrap - correct pattern
void variant10_ring_buffer_safe() {
    int ring[16] = {0};
    int head = 0;

    // Safe ring buffer access with modulo
    for (int i = -5; i <= 5; i++) {
        int idx = (head + i + 16) % 16;  // Always in range [0,15]
        ring[idx] = i;
    }
}

// Variant 11: Ring buffer wrap - bug
void variant11_ring_buffer_bug() {
    int ring[16] = {0};
    int head = 0;

    // BUG: forgot to handle negative properly
    for (int i = -5; i <= 5; i++) {
        int idx = (head + i) % 16;  // BUG: if head+i is negative, result is negative
        ring[idx] = i;              // Negative index!
    }
}

// Variant 12: Two-dimensional array negative
void variant12_2d_negative() {
    int arr[5][5] = {{0}};
    int row = 2;
    int col = -1;
    int x = arr[row][col];  // BUG: negative column  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 13: Negative index in struct access
struct Buffer {
    char data[100];
};

void variant13_struct_negative() {
    Buffer buf = {{0}};
    buf.data[-1] = 'x';  // BUG: negative index  dtt-error {{out of bounds}}
}

// Variant 14: Computed negative from function return
int get_offset() { return -5; }

void variant14_function_return_negative() {
    int arr[10] = {0};
    int offset = get_offset();
    int x = arr[offset];  // BUG: offset is -5  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 15: Negative with pointer to middle
void variant15_middle_pointer() {
    int arr[10] = {0};
    int* middle = arr + 5;

    // Accessing elements before middle via negative index
    int a = middle[-1];  // OK: equivalent to arr[4]
    int b = middle[-5];  // OK: equivalent to arr[0]
    int c = middle[-6];  // BUG: equivalent to arr[-1]  dtt-error {{out of bounds}}
    (void)a; (void)b; (void)c;
}

// Variant 16: Negative from enum cast
enum Direction { LEFT = -1, STAY = 0, RIGHT = 1 };

void variant16_enum_negative() {
    int positions[10] = {0};
    int current = 0;  // BUG: if current is 0 and we go LEFT

    Direction dir = LEFT;
    int x = positions[current + dir];  // BUG: 0 + (-1) = -1  dtt-error {{out of bounds}}
    (void)x;
}

// Variant 17: C-string with negative index
void variant17_string_negative() {
    const char* str = "Hello";
    char c = str[-1];  // KNOWN GAP (not asserted): before string start
    (void)c;
}

// Variant 18: Negative via subtraction from 0
void variant18_subtract_from_zero() {
    int arr[10] = {0};
    int base = 0;
    int delta = 1;
    int idx = base - delta;  // -1
    arr[idx] = 42;  // BUG: negative index  dtt-error {{out of bounds}}
}

// Variant 19: Negative index in memcpy destination
void variant19_memcpy_negative_dest() {
    char buf[100] = {0};
    char* dest = buf + 10;
    char src[] = "test";

    // BUG: copying to negative offset from dest
    memcpy(dest - 20, src, sizeof(src));  // Writes before buf!
}

// Variant 20: Safe case - DTT should NOT flag valid middle-pointer access
void variant20_safe_middle_access() {
    int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int* p = arr + 5;

    // All these are safe
    int a = p[-5];  // arr[0] - OK
    int b = p[-1];  // arr[4] - OK
    int c = p[0];   // arr[5] - OK
    int d = p[4];   // arr[9] - OK

    (void)a; (void)b; (void)c; (void)d;
}

int main() {
    // These demonstrate negative indexing patterns
    // DTT should distinguish intentional patterns from bugs
    return 0;
}
