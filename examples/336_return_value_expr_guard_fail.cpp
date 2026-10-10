// WHAT IS TESTED:
// front() with zero size should fail.
//
// Expected: ERROR

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test_zero_size() {
    Vec v;
    v._size = 0;
    v.front();  // ERROR: _size = 0  dtt-error {{_size > 0}}
}

int main() { test_zero_size(); return 0; }
