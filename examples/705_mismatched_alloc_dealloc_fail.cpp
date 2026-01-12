// 705_mismatched_alloc_dealloc_fail.cpp
// DTT tracks allocation type to detect mismatched deallocators
// Mixing malloc/free with new/delete causes undefined behavior (DTT catches these)

#include <cstdlib>
#include <cstring>
#include <new>

// ============================================================================
// MALLOC WITH DELETE
// ============================================================================

// Variant 1: malloc with delete - simple
void variant1_malloc_delete() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    delete p;  // BUG: malloc'd memory freed with delete
}

// Variant 2: malloc with delete - array
void variant2_malloc_delete_ptr() {
    int* arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i;
    delete arr;  // BUG: malloc'd array freed with scalar delete
}

// Variant 3: malloc with delete[]
void variant3_malloc_delete_array() {
    int* arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i;
    delete[] arr;  // BUG: malloc'd memory freed with delete[]
}

// Variant 4: calloc with delete
void variant4_calloc_delete() {
    int* p = (int*)calloc(1, sizeof(int));
    *p = 42;
    delete p;  // BUG: calloc'd memory freed with delete
}

// Variant 5: calloc with delete[]
void variant5_calloc_delete_array() {
    int* arr = (int*)calloc(10, sizeof(int));
    delete[] arr;  // BUG: calloc'd memory freed with delete[]
}

// ============================================================================
// NEW WITH FREE
// ============================================================================

// Variant 6: new with free - simple
void variant6_new_free() {
    int* p = new int(42);
    free(p);  // BUG: new'd memory freed with free
}

// Variant 7: new object with free
class MyClass {
public:
    int value;
    MyClass(int v) : value(v) {}
    ~MyClass() { /* destructor won't be called with free! */ }
};

void variant7_new_object_free() {
    MyClass* obj = new MyClass(42);
    free(obj);  // BUG: destructor not called, undefined behavior
}

// Variant 8: new[] with free
void variant8_new_array_free() {
    int* arr = new int[10];
    for (int i = 0; i < 10; i++) arr[i] = i;
    free(arr);  // BUG: new[] memory freed with free
}

// Variant 9: new[] objects with free
void variant9_new_object_array_free() {
    MyClass* arr = new MyClass[5]{{1}, {2}, {3}, {4}, {5}};
    free(arr);  // BUG: destructors not called, undefined behavior
}

// ============================================================================
// NEW WITH DELETE[] (scalar/array mismatch)
// ============================================================================

// Variant 10: new with delete[]
void variant10_new_delete_array() {
    int* p = new int(42);
    delete[] p;  // BUG: scalar new freed with array delete[]
}

// Variant 11: new object with delete[]
void variant11_new_object_delete_array() {
    MyClass* obj = new MyClass(42);
    delete[] obj;  // BUG: scalar new freed with delete[]
}

// ============================================================================
// NEW[] WITH DELETE (array/scalar mismatch)
// ============================================================================

// Variant 12: new[] with delete
void variant12_new_array_delete() {
    int* arr = new int[10];
    delete arr;  // BUG: array new[] freed with scalar delete
}

// Variant 13: new[] objects with delete
void variant13_new_object_array_delete() {
    MyClass* arr = new MyClass[5]{{1}, {2}, {3}, {4}, {5}};
    delete arr;  // BUG: only first destructor called, memory leak + UB
}

// ============================================================================
// REALLOC MISMATCHES
// ============================================================================

// Variant 14: realloc on new'd memory
void variant14_realloc_new() {
    int* p = new int(42);
    p = (int*)realloc(p, sizeof(int) * 2);  // BUG: realloc on new'd memory
    free(p);
}

// Variant 15: realloc on new[]'d memory
void variant15_realloc_new_array() {
    int* arr = new int[10];
    arr = (int*)realloc(arr, sizeof(int) * 20);  // BUG: realloc on new[]'d memory
    free(arr);
}

// ============================================================================
// CONDITIONAL/COMPLEX MISMATCHES
// ============================================================================

// Variant 16: Conditional allocation with wrong deallocation
void variant16_conditional_mismatch(bool use_new) {
    int* p;
    if (use_new) {
        p = new int(42);
    } else {
        p = (int*)malloc(sizeof(int));
        *p = 42;
    }
    // Wrong: always uses free regardless of allocation type
    free(p);  // BUG: if use_new was true
}

// Variant 17: Allocation in one function, wrong deallocation in another
int* allocate_with_new() {
    return new int(42);
}

void variant17_cross_function_mismatch() {
    int* p = allocate_with_new();
    free(p);  // BUG: caller doesn't know allocation type
}

// Variant 18: Allocation type lost through void*
void variant18_void_pointer_mismatch() {
    void* p = new int(42);  // Type info lost
    free(p);  // BUG: was allocated with new, freed with free
}

// Variant 19: Wrong deallocation in destructor
class BadContainer {
    int* data;
public:
    BadContainer() : data(new int[10]) {}
    ~BadContainer() {
        free(data);  // BUG: should be delete[]
    }
};

void variant19_destructor_mismatch() {
    BadContainer c;  // Destructor will use wrong deallocator
}

// Variant 20: Array of pointers - mixed allocation
void variant20_mixed_array() {
    int* ptrs[3];
    ptrs[0] = new int(1);
    ptrs[1] = (int*)malloc(sizeof(int));
    ptrs[2] = new int(3);

    // BUG: Using same deallocator for all
    for (int i = 0; i < 3; i++) {
        free(ptrs[i]);  // BUG for ptrs[0] and ptrs[2]
    }
}

// ============================================================================
// PLACEMENT NEW MISMATCHES
// ============================================================================

// Variant 21: Placement new with delete
void variant21_placement_new_delete() {
    char buffer[sizeof(int)];
    int* p = new (buffer) int(42);  // Placement new in stack buffer
    delete p;  // BUG: can't delete stack memory!
}

// Variant 22: Placement new with free
void variant22_placement_new_free() {
    char* buffer = (char*)malloc(sizeof(int));
    int* p = new (buffer) int(42);  // Placement new in malloc'd buffer
    free(p);  // OK technically, but confusing and fragile
    // The proper way would be: p->~int(); free(buffer);
}

// ============================================================================
// ALIGNED ALLOCATION MISMATCHES
// ============================================================================

// Variant 23: aligned_alloc with delete
void variant23_aligned_alloc_delete() {
    int* p = (int*)aligned_alloc(64, sizeof(int) * 16);
    delete p;  // BUG: aligned_alloc should use free()
}

// Variant 24: aligned_alloc with delete[]
void variant24_aligned_alloc_delete_array() {
    int* arr = (int*)aligned_alloc(64, sizeof(int) * 16);
    delete[] arr;  // BUG: aligned_alloc should use free()
}

// ============================================================================
// OPERATOR NEW MISMATCHES
// ============================================================================

// Variant 25: operator new with free
void variant25_operator_new_free() {
    void* p = operator new(sizeof(int));
    new (p) int(42);
    free(p);  // BUG: should use operator delete
}

// Variant 26: operator new[] with delete
void variant26_operator_new_array_delete() {
    void* p = operator new[](sizeof(int) * 10);
    delete p;  // BUG: operator new[] should use operator delete[]
}

// ============================================================================
// SAFE CASES - Correct matching
// ============================================================================

// Variant 27: Correct malloc/free
void variant27_correct_malloc_free() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);  // OK
}

// Variant 28: Correct new/delete
void variant28_correct_new_delete() {
    int* p = new int(42);
    delete p;  // OK
}

// Variant 29: Correct new[]/delete[]
void variant29_correct_new_array_delete_array() {
    int* arr = new int[10];
    delete[] arr;  // OK
}

// Variant 30: Correct calloc/free
void variant30_correct_calloc_free() {
    int* p = (int*)calloc(1, sizeof(int));
    *p = 42;
    free(p);  // OK
}

int main() {
    // These demonstrate mismatched allocator/deallocator pairs
    // DTT should track allocation type and detect mismatches
    return 0;
}
