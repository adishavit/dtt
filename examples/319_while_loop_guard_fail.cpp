// WHAT IS TESTED:
// While-loop guard constraints: wrong variable, wrong object, out of bounds.
//
// Expected: ERROR (constraint violations caught)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// Using wrong variable (not the guarded one)
void test_wrong_variable() {
    Vec v;
    v._size = 3;
    int i = 0;
    int j = 10;  // Unrelated variable, clearly >= _size

    while (i < v._size) {
        v[j] = 1;  // ERROR: j is not guarded by the while condition
        ++i;
    }
}

// Constant index out of bounds inside while
void test_constant_out_of_bounds() {
    Vec v;
    v._size = 3;
    int i = 0;

    while (i < v._size) {
        v[100] = 1;  // ERROR: 100 >= 3
        ++i;
    }
}

// Guard on v1 should NOT prove constraint on v2
void test_wrong_object() {
    Vec v1, v2;
    v1._size = 10;
    v2._size = 2;
    int i = 5;

    while (i < v1._size) {
        v2[i] = 1;  // ERROR: guard on v1 doesn't prove v2
        ++i;
    }
}

int main() { test_wrong_variable(); return 0; }
