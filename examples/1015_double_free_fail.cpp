// 1015_double_free_fail.cpp
// Tests: Double-free and use-after-free detection
// All variants should produce DTT errors

#include <cstdlib>
#include <cstring>

// Variant 1: Double free
void test_double_free() {
    void* p = malloc(100);
    if (p) {
        free(p);
        free(p);  // ERROR: double free
    }
}

// Variant 2: Use after free - memcpy
void test_use_after_free_memcpy() {
    char* p = (char*)malloc(100);
    if (p) {
        free(p);
        memcpy(p, "hello", 5);  // ERROR: use after free
    }
}

// Variant 3: Use after free - memset
void test_use_after_free_memset() {
    char* p = (char*)malloc(100);
    if (p) {
        free(p);
        memset(p, 0, 100);  // ERROR: use after free
    }
}

// Variant 4: Use after free - strcpy
void test_use_after_free_strcpy() {
    char* p = (char*)malloc(100);
    if (p) {
        free(p);
        strcpy(p, "hello");  // ERROR: use after free
    }
}

// Variant 5: Use after free - dereference
void test_use_after_free_deref() {
    int* p = (int*)malloc(sizeof(int));
    if (p) {
        *p = 42;
        free(p);
        int x = *p;  // ERROR: use after free
        (void)x;
    }
}

int main() { return 0; }
