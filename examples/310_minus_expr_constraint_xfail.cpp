// WHAT IS TESTED:
// Expression constraints with subtraction should be parsed as arithmetic
// expressions. The current parser ignores '-' for expression detection and
// mis-parses "arg0 - 1 < _size" as "arg0 < _size", causing a false error.
//
// Expected behavior: PASS (arg0 - 1 < _size holds for arg0 == _size)
// Current behavior: FAIL (interpreted as arg0 < _size)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 - 1 < _size")]]
    int& at(int idx) { return *reinterpret_cast<int*>(0); }
};

void test() {
    Vec v;
    v._size = 3;

    // 3 - 1 < 3 is true, should be allowed.
    v.at(3);
}

int main() { test(); return 0; }
