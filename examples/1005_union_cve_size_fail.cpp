// 1005_union_cve_size_fail.cpp - Size/bounds confusion via type confusion
// Pattern: Different union members have different sizes, leading to overflow

// Classic size confusion: pointer (8 bytes) vs int (4 bytes)
union SizeMismatch {
    int smallInt;          // 4 bytes
    void* largePtr;        // 8 bytes on 64-bit
    long long veryLarge;   // 8 bytes
};

// Variant 1: Writing 8 bytes when expecting 4
void test_write_overflow(SizeMismatch* arr, int count) {
    for (int i = 0; i < count; i++) {
        arr[i].smallInt = i;  // Code thinks 4-byte writes
        // But if caller passes pointer array, stride is 8 bytes
        // Writes to wrong locations
    }
}

// Variant 2: Reading beyond bounds via size confusion
struct SizedUnion {
    int type;  // 0 = int (4 bytes payload), 1 = ptr (8 bytes payload)
    union {
        int intVal;
        void* ptrVal;
    };
};

int test_read_overflow(SizedUnion* arr, int index) {
    // Assumes int array (4-byte elements after type field)
    // But if ptr array, element spacing is different
    return arr[index].intVal;  // FAIL: wrong stride
}

// Buffer copy size confusion
union BufferUnion {
    struct { char data[16]; } small;
    struct { char data[256]; } large;
};

// Variant 3: Copy overflow - source smaller than dest
void test_copy_size_confusion(BufferUnion* dest, BufferUnion* src, int srcType) {
    (void)srcType;  // Ignored!
    // Assumes src is large (256 bytes) but might be small (16 bytes)
    for (int i = 0; i < 256; i++) {
        dest->large.data[i] = src->large.data[i];  // FAIL: reads past small
    }
}

// Array element count confusion (PROPVARIANT pattern)
union ArrayUnion {
    struct { unsigned int count; int* elements; } intArray;    // count = element count
    struct { unsigned int byteSize; char* bytes; } byteArray;  // byteSize = byte count
};

// Variant 4: Element count vs byte count confusion
void test_count_confusion(ArrayUnion* arr) {
    // Assumes intArray, so count is element count
    unsigned int n = arr->intArray.count;  // If byteArray, this is byteSize
    int* elems = arr->intArray.elements;
    for (unsigned i = 0; i < n; i++) {
        (void)elems[i];  // FAIL: if byteArray, n is bytes not elements (4x overflow)
    }
}

// Variant 5: Byte array as int array - info leak
void test_byte_as_int_leak(ArrayUnion* arr, int* output, int outSize) {
    arr->byteArray.byteSize = 4;  // 4 bytes
    // Code reads as int array (count=4 means 4 ints = 16 bytes)
    unsigned int count = arr->intArray.count;  // 4
    int* elems = arr->intArray.elements;
    for (unsigned i = 0; i < count && (int)i < outSize; i++) {
        output[i] = elems[i];  // FAIL: reads 16 bytes when only 4 valid
    }
}

// String length confusion
union StringUnion {
    struct { unsigned short len; char data[254]; } shortString;  // len = chars
    struct { unsigned int byteLen; char* data; } longString;     // byteLen = bytes
};

// Variant 6: Short string length as long string byte length
void test_string_length_confusion(StringUnion* str) {
    str->shortString.len = 10;
    // Code interprets as longString
    unsigned int len = str->longString.byteLen;  // FAIL: 10 in low 16 bits, garbage in high
    char* data = str->longString.data;  // FAIL: shortString.data[0..3] as pointer
    (void)len;
    (void)data;
}

// Struct padding confusion
union PaddedUnion {
    struct { char a; int b; } padded;    // Has 3 bytes padding after 'a'
    struct { int x; } unpadded;          // No padding
};

// Variant 7: Padding bytes exposed via type confusion
int test_padding_exposure(PaddedUnion* u) {
    u->padded.a = 'X';
    u->padded.b = 0x12345678;
    // Read as unpadded - exposes padding bytes
    return u->unpadded.x;  // FAIL: contains 'X' + 3 padding bytes
}

// Flexible array member confusion
union FlexUnion {
    struct { int count; int data[1]; } fixed;     // Pretends to have 1 element
    struct { int size; char buffer[1]; } flex;    // Actually flexible
};

// Variant 8: Fixed array accessed as flexible
void test_flex_array_confusion(FlexUnion* u, int actualCount) {
    u->fixed.count = actualCount;  // Say 100
    // Only 1 int allocated in fixed.data, but code uses count
    for (int i = 0; i < u->fixed.count; i++) {
        u->fixed.data[i] = i;  // FAIL: buffer overflow after index 0
    }
}

int main() {
    SizeMismatch sm[4];
    test_write_overflow(sm, 4);

    SizedUnion su[4];
    test_read_overflow(su, 2);

    BufferUnion dest, src;
    test_copy_size_confusion(&dest, &src, 0);

    ArrayUnion arr;
    arr.intArray.count = 0;
    arr.intArray.elements = nullptr;
    test_count_confusion(&arr);
    int output[10];
    test_byte_as_int_leak(&arr, output, 10);

    StringUnion str;
    test_string_length_confusion(&str);

    PaddedUnion pu;
    test_padding_exposure(&pu);

    FlexUnion fu;
    test_flex_array_confusion(&fu, 10);
    return 0;
}
