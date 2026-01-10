// Example 488: Guard scope — should FAIL
// Guard proofs from an if-statement without an else should NOT remain active
// after the statement. The proof only holds inside the then-block.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Guard leaks after if-without-else
void test_guard_leaks() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        // no-op
    }

    v.front();  // FAIL: _size could be 0
}

// Guard leaks after nested if-without-else
void test_nested_leak() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        if (v._size > 1) {
            // no-op
        }
    }

    v.front();  // FAIL: _size could be 0
}

// Guard leaks from early return path not taken
void test_early_return_leak() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        return;  // Only returns if _size > 0
    }
    // If we reach here, _size <= 0
    v.front();  // FAIL: _size is 0
}

int main() {
    test_guard_leaks();
    test_nested_leak();
    test_early_return_leak();
    return 0;
}
