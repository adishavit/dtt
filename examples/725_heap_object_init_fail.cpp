// 725_heap_object_init_fail.cpp
// DTT detects reading uninitialized heap memory (CWE-457)
// All patterns below should trigger DTT errors

// =============================================================================
// Basic heap allocation patterns
// =============================================================================

// Variant 1: Simple new int (uninitialized)
int variant1_new_int() {
    int* p = new int;    // Default-init: UNINITIALIZED
    int x = *p;          // ERROR: reading uninitialized heap memory
    delete p;
    return x;
}

// =============================================================================
// Aliasing patterns
// =============================================================================

// Variant 2: Read through alias before init is error
int variant2_read_through_alias_uninit() {
    int* p = new int;    // Uninitialized
    int* q = p;          // q aliases p
    int x = *q;          // ERROR: reading uninitialized via alias
    delete p;
    return x;
}

// =============================================================================
// Array allocation patterns
// =============================================================================

// Variant 3: new int[n] (uninitialized array)
int variant3_new_array() {
    int* arr = new int[10];  // Default-init: all elements uninitialized
    int x = arr[0];          // ERROR: reading uninitialized
    delete[] arr;
    return x;
}

// Variant 4: Partial array init
int variant4_partial_array_init() {
    int* arr = new int[10];  // Uninitialized
    arr[0] = 1;              // Only arr[0] initialized
    int x = arr[5];          // ERROR: arr[5] still uninitialized
    delete[] arr;
    return x;
}

// =============================================================================
// Struct/class allocation patterns
// =============================================================================

struct Point {
    int x, y;
};

// Variant 5: new struct (uninitialized members)
int variant5_new_struct() {
    Point* p = new Point;  // Default-init: x and y uninitialized
    int x = p->x;          // ERROR: reading uninitialized member
    delete p;
    return x;
}

// =============================================================================
// Different scalar types
// =============================================================================

// Variant 6: new double (uninitialized)
double variant6_new_double() {
    double* p = new double;  // Uninitialized
    double x = *p;           // ERROR: reading uninitialized
    delete p;
    return x;
}

// Variant 7: new bool (uninitialized)
bool variant7_new_bool() {
    bool* p = new bool;  // Uninitialized
    bool x = *p;         // ERROR: reading uninitialized
    delete p;
    return x;
}

// Variant 8: new char (uninitialized)
char variant8_new_char() {
    char* p = new char;  // Uninitialized
    char x = *p;         // ERROR: reading uninitialized
    delete p;
    return x;
}

// =============================================================================
// Control flow patterns
// =============================================================================

// Variant 9: Conditional init then unconditional read
int variant9_conditional_init(bool cond) {
    int* p = new int;
    if (cond) {
        *p = 42;
    }
    int x = *p;  // ERROR: may be uninitialized if !cond
    delete p;
    return x;
}

int main() {
    return 0;
}
