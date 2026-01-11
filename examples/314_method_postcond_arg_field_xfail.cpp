// WHAT IS TESTED:
// Method postconditions that reference arg0._size are not substituted,
// leaving _size unknown after the call.
//
// Expected behavior: PASS (append should make _size = 2)
// Current behavior: FAIL (postcondition can't use arg0._size)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    [[clang::annotate("dtt:ensures=_size == _size + arg0._size")]]
    void append(const Vec& other) {
        (void)other;
    }
};

void test() {
    Vec a;
    Vec b;
    a._size = 1;
    b._size = 1;

    a.append(b);
    a.at1();
}

int main() { test(); return 0; }
