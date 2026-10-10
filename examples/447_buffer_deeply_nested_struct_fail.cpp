// Example 447: Deeply nested struct array OOB — should FAIL
// Tests that OOB in deeply nested struct arrays is caught

struct Inner {
    int data[4];
};

struct Middle {
    Inner inner;
};

struct Outer {
    Middle mid;
};

void test_deep_oob() {
    Outer o;
    int* p = o.mid.inner.data;  // _size=4

    p[4] = 1;                   // FAIL: 4 >= 4 (one past end)  dtt-error {{out of bounds}}
}

int main() {
    test_deep_oob();
    return 0;
}
