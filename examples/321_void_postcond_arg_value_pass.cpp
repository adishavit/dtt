// WHAT IS TESTED:
// Void postconditions properly substitute argument values (arg0, arg1, etc.)
// Issue #14: FIXED - arg1, arg2 now substituted with actual values

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }
};

// Variant 1: Basic arg1 substitution
[[clang::annotate("dtt:ensures=result._size == arg1")]]
void init(Vec& v, int count) {
    (void)v; (void)count;
}

void variant1_basic() {
    int arr[5] = {};
    Vec v;
    v.data = arr;
    v._size = 0;
    init(v, 5);   // v._size = 5
    v.at4();      // OK: _size = 5 > 4
}

// Variant 2: Multiple arguments (arg1, arg2)
[[clang::annotate("dtt:ensures=result._size == arg1 + arg2")]]
void initSum(Vec& v, int a, int b) {
    (void)v; (void)a; (void)b;
}

void variant2_multiple_args() {
    int arr[5] = {};
    Vec v;
    v.data = arr;
    v._size = 0;
    initSum(v, 2, 3);  // v._size = 2 + 3 = 5
    v.at4();           // OK: _size = 5 > 4
}

// Variant 3: arg0 for output parameter field
struct Container {
    int _count;

    [[clang::annotate("dtt:requires=_count > 0")]]
    void process() {}
};

[[clang::annotate("dtt:ensures=result._count == arg1")]]
void setupContainer(Container& c, int n) {
    (void)c; (void)n;
}

void variant3_arg0_field() {
    Container c;
    c._count = 0;
    setupContainer(c, 10);   // c._count = 10
    c.process();             // OK: _count = 10 > 0
}

// Variant 4: Variable argument (not just literal)
void variant4_variable_arg() {
    int arr[5] = {};
    Vec v;
    v.data = arr;
    v._size = 0;
    int count = 5;
    init(v, count);  // v._size = count (symbolic)
    // Note: This tests that variable refs are handled
}

int main() {
    variant1_basic();
    variant2_multiple_args();
    variant3_arg0_field();
    variant4_variable_arg();
    return 0;
}
