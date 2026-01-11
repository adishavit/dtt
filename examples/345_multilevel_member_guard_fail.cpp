// WHAT IS TESTED:
// Nested member constraint violation should fail.
//
// Expected: ERROR

struct Inner {
    int _size;
};

struct Outer {
    Inner inner;

    [[clang::annotate("dtt:requires=inner._size > 0")]]
    void use() const {}
};

void test_zero_nested() {
    Outer o;
    o.inner._size = 0;
    o.use();  // ERROR: inner._size = 0
}

int main() { test_zero_nested(); return 0; }
