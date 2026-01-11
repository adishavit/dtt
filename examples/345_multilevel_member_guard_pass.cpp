// WHAT IS TESTED:
// Guards on nested member paths should work.
//
// Expected: PASS

struct Inner {
    int _size;
};

struct Outer {
    Inner inner;

    [[clang::annotate("dtt:requires=inner._size > 0")]]
    void use() const {}
};

struct Deep {
    Outer outer;

    [[clang::annotate("dtt:requires=outer.inner._size > 0")]]
    void useDeep() const {}
};

// 1. Simple nested member guard
void test_simple_nested() {
    Outer o;
    o.inner._size = 1;
    if (o.inner._size > 0) {
        o.use();
    }
}

// 2. Known nested value
void test_known_nested() {
    Outer o;
    o.inner._size = 5;
    o.use();  // inner._size = 5 > 0
}

// 3. Deep nesting
void test_deep_nesting() {
    Deep d;
    d.outer.inner._size = 3;
    if (d.outer.inner._size > 0) {
        d.useDeep();
    }
}

// 4. Multiple nested objects
void test_multiple() {
    Outer o1, o2;
    o1.inner._size = 2;
    o2.inner._size = 4;
    if (o1.inner._size > 0) {
        o1.use();
    }
    if (o2.inner._size > 0) {
        o2.use();
    }
}

int main() {
    test_simple_nested();
    test_known_nested();
    test_deep_nesting();
    test_multiple();
    return 0;
}
