// 700_use_after_free_xfail.cpp
// Gap: DTT doesn't track freed state - these are real bugs DTT should catch
// All variants demonstrate use-after-free patterns that compile but are unsafe

#include <cstdlib>
#include <cstring>
#include <cstdio>

// Variant 1: Simple use-after-free - immediate dereference
void variant1_simple_uaf() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);
    int x = *p;  // BUG: use after free - DTT should catch this
    (void)x;
}

// Variant 2: Use-after-free with indirection
void variant2_uaf_indirection() {
    int* p = (int*)malloc(sizeof(int));
    int** pp = &p;
    *p = 42;
    free(p);
    int x = **pp;  // BUG: use after free through double pointer
    (void)x;
}

// Variant 3: Use-after-free in conditional path
void variant3_uaf_conditional(bool flag) {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    if (flag) {
        free(p);
    }
    // p might be freed depending on flag
    int x = *p;  // BUG: potential use after free
    (void)x;
}

// Variant 4: Use-after-free with reassignment between
void variant4_uaf_with_code_between() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);

    int dummy = 0;
    dummy++;  // Some unrelated code

    *p = 100;  // BUG: use after free (write)
    (void)dummy;
}

// Variant 5: Use-after-free of struct member
struct Data {
    int value;
    char* buffer;
};

void variant5_uaf_struct_member() {
    Data* d = (Data*)malloc(sizeof(Data));
    d->buffer = (char*)malloc(100);
    d->value = 42;

    free(d->buffer);
    strcpy(d->buffer, "oops");  // BUG: use after free of member

    free(d);
}

// Variant 6: Use-after-free with array
void variant6_uaf_array() {
    int* arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i;
    free(arr);
    int sum = arr[0] + arr[5];  // BUG: use after free (array access)
    (void)sum;
}

// Variant 7: Use-after-free in loop
void variant7_uaf_in_loop() {
    int* p = (int*)malloc(sizeof(int));
    *p = 0;

    for (int i = 0; i < 5; i++) {
        if (i == 2) free(p);
        *p = i;  // BUG: use after free when i >= 2
    }
}

// Variant 8: Use-after-free with function call
void helper_that_frees(int* p) {
    free(p);
}

void variant8_uaf_via_function() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    helper_that_frees(p);
    int x = *p;  // BUG: use after free (freed by called function)
    (void)x;
}

// Variant 9: Use-after-free of returned pointer
int* allocate_and_free() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);
    return p;  // Returns freed pointer
}

void variant9_uaf_returned_freed() {
    int* p = allocate_and_free();
    int x = *p;  // BUG: use after free (pointer was freed before return)
    (void)x;
}

// Variant 10: Use-after-free with realloc failure
void variant10_uaf_realloc_fail() {
    char* p = (char*)malloc(10);
    strcpy(p, "hello");

    // If realloc fails, it returns NULL but doesn't free original
    // If realloc succeeds, original is freed
    char* new_p = (char*)realloc(p, 1000000000000ULL);  // Likely to fail
    if (new_p == nullptr) {
        // p is still valid here - this is OK
        p[0] = 'H';
    } else {
        // p is freed, new_p is valid
        p[0] = 'H';  // BUG: use after free if realloc succeeded
    }
    free(new_p ? new_p : p);
}

// Variant 11: Use-after-free with calloc
void variant11_uaf_calloc() {
    int* p = (int*)calloc(5, sizeof(int));
    p[0] = 1;
    p[4] = 5;
    free(p);
    int x = p[2];  // BUG: use after free (calloc-allocated)
    (void)x;
}

// Variant 12: Use-after-free comparing pointers
void variant12_uaf_pointer_comparison() {
    int* p = (int*)malloc(sizeof(int));
    int* q = p;
    free(p);

    if (p == q) {  // Technically UB to even use p after free
        *q = 42;   // BUG: use after free through alias
    }
}

// ============================================================================
// NEW/DELETE VARIANTS
// ============================================================================

// Variant 13: Simple use-after-delete
void variant13_uaf_new_delete() {
    int* p = new int(42);
    delete p;
    int x = *p;  // BUG: use after delete
    (void)x;
}

// Variant 14: Use-after-delete with class
class MyClass {
public:
    int value;
    MyClass(int v) : value(v) {}
    int getValue() const { return value; }
};

void variant14_uaf_class_delete() {
    MyClass* obj = new MyClass(42);
    delete obj;
    int x = obj->getValue();  // BUG: use after delete (method call)
    (void)x;
}

// Variant 15: Use-after-delete with virtual
class Base {
public:
    virtual ~Base() = default;
    virtual int compute() { return 1; }
};

class Derived : public Base {
public:
    int compute() override { return 2; }
};

void variant15_uaf_virtual_delete() {
    Base* obj = new Derived();
    delete obj;
    int x = obj->compute();  // BUG: use after delete (virtual call)
    (void)x;
}

// Variant 16: Use-after-delete array access
void variant16_uaf_delete_array() {
    int* arr = new int[10];
    for (int i = 0; i < 10; i++) arr[i] = i;
    delete[] arr;
    int x = arr[5];  // BUG: use after delete[]
    (void)x;
}

// Variant 17: Use-after-delete array of objects
void variant17_uaf_object_array() {
    MyClass* arr = new MyClass[5]{{1}, {2}, {3}, {4}, {5}};
    delete[] arr;
    int x = arr[2].getValue();  // BUG: use after delete[] on object array
    (void)x;
}

// Variant 18: Delete then write
void variant18_uaf_delete_write() {
    int* p = new int(0);
    delete p;
    *p = 42;  // BUG: write after delete
}

// Variant 19: Delete in destructor, access after
class ContainerWithPointer {
    int* data;
public:
    ContainerWithPointer() : data(new int(42)) {}
    ~ContainerWithPointer() { delete data; }
    int* getData() { return data; }
};

void variant19_uaf_destructor_access() {
    int* leaked = nullptr;
    {
        ContainerWithPointer c;
        leaked = c.getData();
    }  // c destroyed here, data deleted
    int x = *leaked;  // BUG: use after delete (via destructor)
    (void)x;
}

// ============================================================================
// BUFFER VARIANTS (char arrays, string operations)
// ============================================================================

// Variant 20: Use-after-free buffer - strcpy
void variant20_uaf_buffer_strcpy() {
    char* buf = (char*)malloc(100);
    strcpy(buf, "Hello");
    free(buf);
    strcpy(buf, "World");  // BUG: strcpy to freed buffer
}

// Variant 21: Use-after-free buffer - strcat
void variant21_uaf_buffer_strcat() {
    char* buf = (char*)malloc(100);
    strcpy(buf, "Hello");
    free(buf);
    strcat(buf, " World");  // BUG: strcat to freed buffer
}

// Variant 22: Use-after-free buffer - strlen
void variant22_uaf_buffer_strlen() {
    char* buf = (char*)malloc(100);
    strcpy(buf, "Hello");
    free(buf);
    size_t len = strlen(buf);  // BUG: strlen on freed buffer
    (void)len;
}

// Variant 23: Use-after-free buffer - memcpy
void variant23_uaf_buffer_memcpy() {
    char* src = (char*)malloc(100);
    char* dst = (char*)malloc(100);
    strcpy(src, "Hello");
    free(src);
    memcpy(dst, src, 10);  // BUG: memcpy from freed source
    free(dst);
}

// Variant 24: Use-after-free buffer - memset
void variant24_uaf_buffer_memset() {
    char* buf = (char*)malloc(100);
    free(buf);
    memset(buf, 0, 100);  // BUG: memset on freed buffer
}

// Variant 25: Use-after-free buffer - sprintf
void variant25_uaf_buffer_sprintf() {
    char* buf = (char*)malloc(100);
    free(buf);
    sprintf(buf, "Value: %d", 42);  // BUG: sprintf to freed buffer
}

// Variant 26: Use-after-delete char buffer
void variant26_uaf_new_char_array() {
    char* buf = new char[100];
    strcpy(buf, "Hello");
    delete[] buf;
    char c = buf[0];  // BUG: access after delete[]
    (void)c;
}

// Variant 27: Use-after-free in string comparison
void variant27_uaf_strcmp() {
    char* s1 = (char*)malloc(10);
    strcpy(s1, "test");
    free(s1);
    int cmp = strcmp(s1, "test");  // BUG: strcmp on freed string
    (void)cmp;
}

// Variant 28: Use-after-free buffer via pointer arithmetic
void variant28_uaf_buffer_arithmetic() {
    char* buf = (char*)malloc(100);
    char* ptr = buf + 50;  // ptr points into buf
    free(buf);
    *ptr = 'x';  // BUG: ptr still points to freed memory
}

// Variant 29: Use-after-free of std::string's internal buffer (conceptual)
// Note: This shows the pattern even though std::string manages its own memory
void variant29_uaf_string_data() {
    char* data = nullptr;
    {
        char* buf = (char*)malloc(100);
        strcpy(buf, "Hello World");
        data = buf;
        free(buf);
    }
    // data now points to freed memory
    char c = data[0];  // BUG: access freed buffer
    (void)c;
}

// Variant 30: Buffer use-after-free with binary data
void variant30_uaf_binary_buffer() {
    unsigned char* buf = (unsigned char*)malloc(256);
    for (int i = 0; i < 256; i++) buf[i] = (unsigned char)i;
    free(buf);
    unsigned char val = buf[128];  // BUG: read from freed binary buffer
    (void)val;
}

int main() {
    // These would crash or cause undefined behavior at runtime
    // DTT should catch them at compile time
    return 0;
}
