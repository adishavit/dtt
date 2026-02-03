// 725_heap_object_init_pass.cpp
// Safe heap allocation patterns that should compile without DTT errors
// All patterns ensure heap memory is initialized before reading

// =============================================================================
// Value-initialized allocations (always safe)
// =============================================================================

// Pattern 1: new T() - value initialization
int pattern1_value_init() {
    int* p = new int();  // Zero-initialized
    int x = *p;
    delete p;
    return x;
}

// Pattern 2: new T{} - brace initialization
int pattern2_brace_init() {
    int* p = new int{};  // Zero-initialized
    int x = *p;
    delete p;
    return x;
}

// Pattern 3: new T(value) - direct initialization
int pattern3_direct_init() {
    int* p = new int(42);
    int x = *p;
    delete p;
    return x;
}

// Pattern 4: new T{value} - brace with value
int pattern4_brace_value() {
    int* p = new int{42};
    int x = *p;
    delete p;
    return x;
}

// =============================================================================
// Write before read patterns
// =============================================================================

// Pattern 5: Explicit assignment before read
int pattern5_assign_before_read() {
    int* p = new int;
    *p = 100;    // Write first
    int x = *p;  // Then read
    delete p;
    return x;
}

// Pattern 6: Write through alias before read
int pattern6_alias_write() {
    int* p = new int;
    int* q = p;
    *q = 50;     // Write through alias
    int x = *p;  // Read through original
    delete p;
    return x;
}

// Pattern 7: Multiple writes
int pattern7_multiple_writes() {
    int* p = new int;
    *p = 1;
    *p = 2;
    *p = 3;
    int x = *p;
    delete p;
    return x;
}

// =============================================================================
// Array patterns
// =============================================================================

// Pattern 8: Value-initialized array
int pattern8_array_value_init() {
    int* arr = new int[10]();  // All elements zero
    int x = arr[5];
    delete[] arr;
    return x;
}

// Pattern 9: Array with explicit init
int pattern9_array_explicit_init() {
    int* arr = new int[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = i;  // Initialize all elements
    }
    int x = arr[5];
    delete[] arr;
    return x;
}

// =============================================================================
// Struct patterns
// =============================================================================

struct Point {
    int x, y;
};

// Pattern 10: Value-initialized struct
int pattern10_struct_value_init() {
    Point* p = new Point();  // x=0, y=0
    int x = p->x;
    delete p;
    return x;
}

// Pattern 11: Aggregate-initialized struct
int pattern11_struct_aggregate() {
    Point* p = new Point{10, 20};
    int x = p->x + p->y;
    delete p;
    return x;
}

// Pattern 12: Struct with explicit member init
int pattern12_struct_member_init() {
    Point* p = new Point;
    p->x = 5;
    p->y = 10;
    int sum = p->x + p->y;
    delete p;
    return sum;
}

// =============================================================================
// Control flow patterns (all paths initialize)
// =============================================================================

// Pattern 13: Init in both branches
int pattern13_both_branches(bool cond) {
    int* p = new int;
    if (cond) {
        *p = 1;
    } else {
        *p = 2;
    }
    int x = *p;  // OK: all paths initialize
    delete p;
    return x;
}

// Pattern 14: Init before branch, read after
int pattern14_init_before_branch(bool cond) {
    int* p = new int;
    *p = 42;  // Init before branch
    if (cond) {
        *p += 1;
    }
    int x = *p;
    delete p;
    return x;
}

// =============================================================================
// Different types (value-init forms)
// =============================================================================

// Pattern 15: Various types value-initialized
void pattern15_various_types() {
    double* d = new double();
    bool* b = new bool();
    char* c = new char();

    double dv = *d;
    bool bv = *b;
    char cv = *c;

    delete d;
    delete b;
    delete c;

    (void)dv; (void)bv; (void)cv;
}

// =============================================================================
// Class with constructor (always initialized)
// =============================================================================

class Widget {
public:
    int value;
    Widget() : value(0) {}  // Constructor initializes
    Widget(int v) : value(v) {}
};

// Pattern 16: Class with default constructor
int pattern16_class_default_ctor() {
    Widget* w = new Widget;  // Constructor runs, value=0
    int x = w->value;        // OK: constructor initialized it
    delete w;
    return x;
}

// Pattern 17: Class with parameterized constructor
int pattern17_class_param_ctor() {
    Widget* w = new Widget(42);
    int x = w->value;
    delete w;
    return x;
}

int main() {
    pattern1_value_init();
    pattern5_assign_before_read();
    pattern13_both_branches(true);
    return 0;
}
