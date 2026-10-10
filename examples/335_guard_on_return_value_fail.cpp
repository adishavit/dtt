// WHAT IS TESTED:
// front() without size guard should fail.
//
// Expected: ERROR

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test_no_guard() {
    Vec v;
    v._size = 0;
    v.front();  // ERROR: _size = 0, not > 0  dtt-error {{_size > 0}}
}

int main() { test_no_guard(); return 0; }
