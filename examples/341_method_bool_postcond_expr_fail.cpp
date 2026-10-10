// WHAT IS TESTED:
// at1() without size >= 2 should fail.
//
// Expected: ERROR

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }
};

void test_too_small() {
    Vec v;
    v._size = 1;
    v.at1();  // ERROR: _size = 1, not > 1  dtt-error {{_size > 1}}
}

int main() { test_too_small(); return 0; }
