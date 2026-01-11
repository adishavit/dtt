// WHAT IS TESTED:
// Guards using struct field as index should be recognized.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

struct State {
    int i;
    int j;
};

// 1. Simple struct field index
void test_struct_field(State s) {
    Vec v;
    v._size = 3;
    if (s.i < v._size) {
        v[s.i] = 1;
    }
}

// 2. Multiple struct fields
void test_multiple_fields(State s) {
    Vec v;
    v._size = 5;
    if (s.i < v._size) {
        v[s.i] = 1;
    }
    if (s.j < v._size) {
        v[s.j] = 2;
    }
}

// 3. Nested struct field
struct Outer {
    State inner;
};

void test_nested_field(Outer o) {
    Vec v;
    v._size = 3;
    if (o.inner.i < v._size) {
        v[o.inner.i] = 1;
    }
}

// 4. Struct pointer field
void test_ptr_field(State* s) {
    Vec v;
    v._size = 3;
    if (s != nullptr && s->i < v._size) {
        v[s->i] = 1;
    }
}

int main() {
    State s{1, 2};
    test_struct_field(s);
    test_multiple_fields(s);
    Outer o{{1, 2}};
    test_nested_field(o);
    test_ptr_field(&s);
    return 0;
}
