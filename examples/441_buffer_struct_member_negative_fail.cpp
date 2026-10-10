// Example 441: Struct member array negative index — should FAIL
// Tests that negative indices on struct member arrays are caught

struct Buffer {
    int data[10];
};

void test_negative_index() {
    Buffer buf;
    int* p = buf.data;     // _size=10, _offset=0

    p[-1] = 1;             // FAIL: -1 < 0 (before start)  dtt-error {{out of bounds}}
}

int main() {
    test_negative_index();
    return 0;
}
