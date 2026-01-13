// 702_allocation_size_tracking_xfail.cpp
// Gap: DTT doesn't track allocation sizes to detect buffer overflows
// All variants demonstrate OOB access that compiles but is unsafe

#include <cstdlib>
#include <cstring>
#include <cstdio>

// Variant 1: Simple buffer overflow - off by one
void variant1_off_by_one() {
    int* arr = (int*)malloc(10 * sizeof(int));
    arr[10] = 42;  // BUG: OOB write (valid indices are 0-9)
    free(arr);
}

// Variant 2: Buffer overflow - way past end
void variant2_way_past_end() {
    int* arr = (int*)malloc(10 * sizeof(int));
    arr[100] = 42;  // BUG: way out of bounds
    free(arr);
}

// Variant 3: Buffer underflow - negative index
void variant3_negative_index() {
    int* arr = (int*)malloc(10 * sizeof(int));
    arr[-1] = 42;  // BUG: negative index (underflow)
    free(arr);
}

// Variant 4: Pointer arithmetic past allocation
void variant4_pointer_arithmetic_overflow() {
    int* arr = (int*)malloc(10 * sizeof(int));
    int* p = arr + 10;  // Points one past end (OK for comparison)
    *p = 42;            // BUG: dereference of one-past-end pointer
    free(arr);
}

// Variant 5: Pointer arithmetic underflow
void variant5_pointer_arithmetic_underflow() {
    int* arr = (int*)malloc(10 * sizeof(int));
    int* p = arr - 1;   // Points before start
    *p = 42;            // BUG: dereference before allocation
    free(arr);
}

// Variant 6: Size mismatch - allocated chars, accessed as ints
void variant6_type_size_mismatch() {
    void* buf = malloc(10);  // 10 bytes
    int* arr = (int*)buf;
    arr[2] = 42;  // BUG: arr[2] needs offset 8-11, but only 10 bytes allocated
    free(buf);
}

// Variant 7: Computed index overflow
void variant7_computed_index_overflow() {
    int* arr = (int*)malloc(10 * sizeof(int));
    int i = 5;
    int j = 6;
    arr[i + j] = 42;  // BUG: 5+6=11, out of bounds
    free(arr);
}

// Variant 8: Loop exceeds bounds
void variant8_loop_overflow() {
    int* arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i <= 10; i++) {  // BUG: <= should be <
        arr[i] = i;
    }
    free(arr);
}

// Variant 9: Calloc size tracking
void variant9_calloc_overflow() {
    int* arr = (int*)calloc(5, sizeof(int));  // 5 elements
    arr[5] = 42;  // BUG: OOB (valid indices 0-4)
    free(arr);
}

// Variant 10: Realloc size not tracked
void variant10_realloc_size() {
    int* arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i;

    arr = (int*)realloc(arr, 5 * sizeof(int));  // Shrunk to 5 elements
    int x = arr[7];  // BUG: OOB after realloc shrunk the buffer
    (void)x;
    free(arr);
}

// Variant 11: Strncpy buffer overflow
void variant11_strncpy_overflow() {
    char* buf = (char*)malloc(10);
    strncpy(buf, "This string is way too long for the buffer", 50);  // BUG: writes past buf
    free(buf);
}

// Variant 12: Memcpy overflow
void variant12_memcpy_overflow() {
    char* dst = (char*)malloc(10);
    char src[50] = "This is a long source string that won't fit";
    memcpy(dst, src, 50);  // BUG: copies 50 bytes to 10-byte buffer
    free(dst);
}

// Variant 13: Memset overflow
void variant13_memset_overflow() {
    char* buf = (char*)malloc(10);
    memset(buf, 0, 20);  // BUG: zeroes 20 bytes in 10-byte buffer
    free(buf);
}

// Variant 14: Struct member access past allocation
struct Header {
    int type;
    int flags;
    char data[100];
};

void variant14_struct_size_mismatch() {
    // Only allocate enough for header fields, not the full struct
    Header* h = (Header*)malloc(sizeof(int) * 2);  // Only 8 bytes
    h->type = 1;   // OK
    h->flags = 2;  // OK
    h->data[0] = 'x';  // BUG: data[] is beyond allocated size
    free(h);
}

// Variant 15: String operations on undersized buffer
void variant15_string_overflow() {
    char* str = (char*)malloc(5);
    strcpy(str, "Hello World");  // BUG: "Hello World" is 12 chars including null
    free(str);
}

// Variant 16: sprintf overflow
void variant16_sprintf_overflow() {
    char* buf = (char*)malloc(10);
    sprintf(buf, "Value: %d, Name: %s", 12345, "test");  // BUG: result exceeds 10 chars
    free(buf);
}

// Variant 17: Pointer arithmetic with size tracking
void variant17_arithmetic_chain() {
    char* buf = (char*)malloc(100);
    char* end = buf + 100;  // Points to one-past-end

    char* p = buf;
    p += 50;   // p now at offset 50, OK
    p += 50;   // p now at offset 100 (one-past-end), OK for arithmetic
    p += 1;    // p now at offset 101, past one-past-end
    char c = *p;  // BUG: dereference way out of bounds
    (void)c;
    free(buf);
}

// Variant 18: Multi-dimensional access simulation
void variant18_multidim_overflow() {
    int* arr = (int*)malloc(3 * 4 * sizeof(int));  // 3x4 = 12 elements

    // Simulating arr[row][col] as arr[row * 4 + col]
    int row = 3;  // BUG: row 3 is out of bounds (valid: 0-2)
    int col = 0;
    arr[row * 4 + col] = 42;  // Accesses index 12, but only 0-11 valid

    free(arr);
}

// Variant 19: VLA-style but heap allocated
void variant19_vla_style_overflow(int n) {
    int* arr = (int*)malloc(n * sizeof(int));
    arr[n] = 42;  // BUG: arr[n] is always OOB regardless of n
    free(arr);
}

// Variant 20: Safe case - DTT should NOT flag this
void variant20_safe_allocation_use() {
    int* arr = (int*)malloc(10 * sizeof(int));
    if (!arr) return;

    for (int i = 0; i < 10; i++) {
        arr[i] = i;  // OK: indices 0-9 within bounds
    }
    arr[9] = 42;  // OK: last valid index

    free(arr);
}

int main() {
    // These demonstrate buffer overflows that DTT should catch
    // by tracking allocation sizes through the code
    return 0;
}
