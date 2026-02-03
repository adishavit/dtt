// 725_heap_object_init_xfail.cpp
// Gap: DTT doesn't track initialization state of heap objects
// new T (default-init) leaves object uninitialized
// new T() (value-init) zero-initializes the object
// Reading uninitialized heap memory is undefined behavior (CWE-457)

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

// Variant 2: new int() is OK (value-initialized to 0)
int variant2_new_int_value_init() {
    int* p = new int();  // Value-init: initialized to 0
    int x = *p;          // OK: value-initialized
    delete p;
    return x;
}

// Variant 3: new int(42) is OK (direct-initialized)
int variant3_new_int_direct_init() {
    int* p = new int(42);  // Direct-init: initialized to 42
    int x = *p;            // OK: direct-initialized
    delete p;
    return x;
}

// Variant 4: new int{} is OK (value-initialized via braces)
int variant4_new_int_brace_init() {
    int* p = new int{};  // Value-init: initialized to 0
    int x = *p;          // OK: value-initialized
    delete p;
    return x;
}

// =============================================================================
// Aliasing patterns (ResourceId approach handles these)
// =============================================================================

// Variant 5: Write through alias initializes the object
int variant5_write_through_alias() {
    int* p = new int;    // Uninitialized
    int* q = p;          // q aliases p
    *q = 42;             // Write through q initializes the heap object
    int x = *p;          // OK: initialized via alias q
    delete p;
    return x;
}

// Variant 6: Read through alias before init is error
int variant6_read_through_alias_uninit() {
    int* p = new int;    // Uninitialized
    int* q = p;          // q aliases p
    int x = *q;          // ERROR: reading uninitialized via alias
    delete p;
    return x;
}

// Variant 7: Multiple aliases, write through one
int variant7_multiple_aliases() {
    int* p = new int;    // Uninitialized
    int* q = p;
    int* r = q;
    *r = 100;            // Write through r
    int x = *p;          // OK: initialized via alias chain
    delete p;
    return x;
}

// =============================================================================
// Array allocation patterns
// =============================================================================

// Variant 8: new int[n] (uninitialized array)
int variant8_new_array() {
    int* arr = new int[10];  // Default-init: all elements uninitialized
    int x = arr[0];          // ERROR: reading uninitialized
    delete[] arr;
    return x;
}

// Variant 9: new int[n]() (value-initialized array)
int variant9_new_array_value_init() {
    int* arr = new int[10]();  // Value-init: all elements 0
    int x = arr[0];            // OK: value-initialized
    delete[] arr;
    return x;
}

// Variant 10: Partial array init
int variant10_partial_array_init() {
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

// Variant 11: new struct (uninitialized members)
int variant11_new_struct() {
    Point* p = new Point;  // Default-init: x and y uninitialized
    int x = p->x;          // ERROR: reading uninitialized member
    delete p;
    return x;
}

// Variant 12: new struct() (value-initialized members)
int variant12_new_struct_value_init() {
    Point* p = new Point();  // Value-init: x=0, y=0
    int x = p->x;            // OK: value-initialized
    delete p;
    return x;
}

// Variant 13: new struct{} (aggregate-initialized)
int variant13_new_struct_brace_init() {
    Point* p = new Point{1, 2};  // Direct-init: x=1, y=2
    int x = p->x;                // OK: initialized
    delete p;
    return x;
}

// =============================================================================
// Different scalar types
// =============================================================================

// Variant 14: new double (uninitialized)
double variant14_new_double() {
    double* p = new double;  // Uninitialized
    double x = *p;           // ERROR: reading uninitialized
    delete p;
    return x;
}

// Variant 15: new bool (uninitialized)
bool variant15_new_bool() {
    bool* p = new bool;  // Uninitialized
    bool x = *p;         // ERROR: reading uninitialized
    delete p;
    return x;
}

// Variant 16: new char (uninitialized)
char variant16_new_char() {
    char* p = new char;  // Uninitialized
    char x = *p;         // ERROR: reading uninitialized
    delete p;
    return x;
}

// =============================================================================
// Control flow patterns
// =============================================================================

// Variant 17: Conditional init then unconditional read
int variant17_conditional_init(bool cond) {
    int* p = new int;
    if (cond) {
        *p = 42;
    }
    int x = *p;  // ERROR: may be uninitialized if !cond
    delete p;
    return x;
}

// Variant 18: Init in both branches
int variant18_init_both_branches(bool cond) {
    int* p = new int;
    if (cond) {
        *p = 1;
    } else {
        *p = 2;
    }
    int x = *p;  // OK: initialized on all paths
    delete p;
    return x;
}

// =============================================================================
// Safe patterns (should NOT trigger errors)
// =============================================================================

// Safe 1: Write before read
int safe1_write_before_read() {
    int* p = new int;
    *p = 42;         // Write first
    int x = *p;      // OK: initialized
    delete p;
    return x;
}

// Safe 2: Value-init forms
int safe2_value_init_forms() {
    int* a = new int();   // OK
    int* b = new int{};   // OK
    int* c = new int(0);  // OK
    int sum = *a + *b + *c;
    delete a; delete b; delete c;
    return sum;
}

int main() {
    return 0;
}
