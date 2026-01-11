// WHAT IS TESTED:
// Method bool postconditions with arithmetic expressions are not supported.
//
// Expected behavior: PASS (ok implies _size >= 2)
// Current behavior: FAIL (no proof from expression postcondition)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:ensures_if_true=_size >= 2")]]
    bool ok() const { return _size >= 2; }

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }
};

void test() {
    Vec v;
    v._size = 2;
    if (v.ok()) {
        v.at1();
    }
}

int main() { test(); return 0; }
