// Example 307: Guard scope — should PASS
// Guard proofs from if-statement should be active inside the then-block

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Guard active inside then-block
void test_guard_inside_then() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        v.front();  // OK: inside guarded block
    }
}

// Guard active with else present
void test_guard_with_else() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        v.front();  // OK: inside guarded block
    } else {
        // _size <= 0 here, no front() call
    }
}

// Nested guards
void test_nested_guards() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        if (v._size > 1) {
            v.front();  // OK: doubly guarded
        }
    }
}

int main() {
    test_guard_inside_then();
    test_guard_with_else();
    test_nested_guards();
    return 0;
}
