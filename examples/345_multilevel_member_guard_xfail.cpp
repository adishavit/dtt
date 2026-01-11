// WHAT IS TESTED:
// Guards on nested member paths are not resolved.
//
// Expected behavior: PASS (guard proves outer.inner._size > 0)
// Current behavior: FAIL (path not resolved)

struct Inner {
    int _size;
};

struct Outer {
    Inner inner;

    [[clang::annotate("dtt:requires=inner._size > 0")]]
    void use() const {}
};

void test() {
    Outer o;
    o.inner._size = 1;

    if (o.inner._size > 0) {
        o.use();
    }
}

int main() { test(); return 0; }
