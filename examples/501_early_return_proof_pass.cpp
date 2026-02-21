// Example 501: Early return/exit proof propagation — should COMPILE
// After `if (cond) { <terminating-stmt>; }`, the negation of cond should hold

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// Test: return
void test_return(Vec& v) {
    if (v._size <= 0) {
        return;
    }
    v.front();  // OK: _size > 0
}

// Test: throw
void test_throw(Vec& v) {
    if (v._size <= 0) {
        throw "empty";
    }
    v.front();  // OK: _size > 0
}

// Test: break (in loop context)
void test_break(Vec& v) {
    while (true) {
        if (v._size <= 0) {
            break;
        }
        v.front();  // OK: _size > 0
        break;
    }
}

// Test: continue (in loop context)
void test_continue(Vec& v, int n) {
    for (int i = 0; i < n; ++i) {
        if (v._size <= 0) {
            continue;
        }
        v.front();  // OK: _size > 0
    }
}

// Multiple early exits
void test_multiple_exits(Vec& v, bool flag) {
    if (v._size <= 0) {
        return;
    }
    if (flag) {
        return;  // Unrelated exit doesn't affect proof
    }
    v.front();  // OK: _size > 0 still holds
}

int main() {
    int arr[] = {1, 2, 3, 4, 5};
    Vec v;
    v.data = arr;
    v._size = 5;
    test_return(v);
    test_throw(v);
    test_break(v);
    test_continue(v, 1);
    test_multiple_exits(v, false);
    return 0;
}
