// Safe range iteration is accepted. This does not prove a relation between
// the independent index and the range length; see 329_range_for_bounds_xfail.cpp.

#include <initializer_list>

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx);
};

void test() {
    Vec v;
    v._size = 3;
    int i = 0;

    for (int value : {1, 2, 3}) {
        (void)value;
        v[i] = 1;
        ++i;
    }
}

int main() { test(); return 0; }
