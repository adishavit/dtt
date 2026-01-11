// WHAT IS TESTED:
// Expression constraints on fields don't substitute _size correctly.
//
// Expected behavior: PASS (_size + 1 > 0 is always true)
// Current behavior: FAIL (expression uses unresolved _size)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=_size + 1 > 0")]]
    void check() const {}
};

void test() {
    Vec v;
    v._size = 0;
    v.check();
}

int main() { test(); return 0; }
