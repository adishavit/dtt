// WHAT IS TESTED:
// Auto-postcondition inference for out-parameter (reference) modifications.
// Pattern: void init(Vec& v, int n) { v._size = n; } → ensures=arg0._size == arg1
//
// EXPECTED: DTT infers postconditions from reference parameter field assignments
// and applies them at call sites to track the modified object.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }
};

// Free function that modifies a reference parameter
// NO annotation - DTT should infer ensures=arg0._size == arg1
void initVec(Vec& v, int n) {
    v._size = n;
}

// Free function with constant assignment
// NO annotation - DTT should infer ensures=arg0._size == 10
void initToTen(Vec& v) {
    v._size = 10;
}

// Free function that clears
// NO annotation - DTT should infer ensures=arg0._size == 0
void clearVec(Vec& v) {
    v._size = 0;
}

// Variant 1: Initialize with constant via free function
void variant1_init_constant() {
    int arr[10];
    Vec v;
    v.data = arr;
    v._size = 0;
    initToTen(v);  // v._size = 10
    v.at4();       // OK: _size == 10 > 4
}

// Variant 2: Initialize with parameter via free function
void variant2_init_param() {
    int arr[5];
    Vec v;
    v.data = arr;
    v._size = 0;
    initVec(v, 5); // v._size = 5
    v.at4();       // OK: _size == 5 > 4
}

// Variant 3: Chain of free function calls
void variant3_chain() {
    int arr[10];
    Vec v;
    v.data = arr;
    v._size = 0;
    initToTen(v);  // v._size = 10
    v.front();     // OK: 10 > 0
    initVec(v, 3); // v._size = 3
    v.front();     // OK: 3 > 0
}

int main() {
    variant1_init_constant();
    variant2_init_param();
    variant3_chain();
    return 0;
}
