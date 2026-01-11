// WHAT IS TESTED:
// While-loop conditions should create proofs for the loop body.
//
// Expected: PASS (while guard proves constraint)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Basic while loop with < condition
void test_basic_while() {
    Vec v;
    v._size = 3;
    int i = 0;

    while (i < v._size) {
        v[i] = 1;
        ++i;
    }
}

// While with pre-declared loop variable
void test_while_predeclared() {
    Vec v;
    v._size = 5;
    int idx;
    idx = 0;

    while (idx < v._size) {
        v[idx] = 2;
        ++idx;
    }
}

// Multiple iterations with different access patterns
void test_while_multiple_accesses() {
    Vec v;
    v._size = 10;
    int i = 0;

    while (i < v._size) {
        v[i] = i;       // First access
        v[i] = i * 2;   // Second access at same index
        i++;
    }
}

// Reversed condition: bound > var instead of var < bound
void test_while_reversed_condition() {
    Vec v;
    v._size = 4;
    int i = 0;

    while (v._size > i) {  // Same as i < v._size but reversed
        v[i] = 1;
        ++i;
    }
}

// Nested while loops with different variables
void test_nested_while() {
    Vec v;
    v._size = 3;
    int i = 0;

    while (i < v._size) {
        int j = 0;
        while (j < v._size) {
            v[j] = i + j;  // Inner loop guard proves j < _size
            ++j;
        }
        v[i] = 1;  // Outer loop guard proves i < _size
        ++i;
    }
}

// Two different vectors in same loop
void test_while_two_vectors() {
    Vec v1, v2;
    v1._size = 5;
    v2._size = 5;
    int i = 0;

    while (i < v1._size) {
        v1[i] = 1;  // Guarded by i < v1._size
        ++i;
    }

    i = 0;
    while (i < v2._size) {
        v2[i] = 2;  // Guarded by i < v2._size
        ++i;
    }
}

int main() {
    test_basic_while();
    test_while_predeclared();
    test_while_multiple_accesses();
    test_while_reversed_condition();
    test_nested_while();
    test_while_two_vectors();
    return 0;
}
