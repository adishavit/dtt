// WHAT IS TESTED:
// do-while with unsafe first iteration should be caught.
// Unlike while-loops, do-while executes body before checking condition.
//
// Expected: ERROR (first iteration not provably safe)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

int get_value();  // Unknown runtime value

// First iteration uses unknown value - can't prove safety
void test_unknown_start() {
    Vec v;
    v._size = 3;
    int i = get_value();  // Unknown, could be >= _size

    do {
        v[i] = 1;  // ERROR: can't prove i < _size on first iteration  dtt-error {{i < 3}}
        ++i;
    } while (i < v._size);
}

// Constant out of bounds on first iteration
void test_constant_out_of_bounds() {
    Vec v;
    v._size = 3;
    int i = 10;  // 10 >= 3

    do {
        v[i] = 1;  // ERROR: 10 >= 3  dtt-error {{out of bounds}}
        ++i;
    } while (i < v._size);
}

// Wrong object (guard on v1, access on v2)
void test_wrong_object() {
    Vec v1, v2;
    v1._size = 10;
    v2._size = 2;
    int i = 5;

    do {
        v2[i] = 1;  // ERROR: 5 >= 2  dtt-error {{out of bounds}}
        ++i;
    } while (i < v1._size);
}

int main() { test_unknown_start(); return 0; }
