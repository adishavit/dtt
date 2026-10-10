// Example 431: Struct member array — should FAIL
// Tests that array members of structs are tracked
//
// s.member for struct { T member[N]; } tracks as N-element buffer

struct Buffer {
    int data[10];
};

void test_struct_member_array() {
    Buffer buf;
    int* p = buf.data;     // _size=10

    p[10] = 1;             // FAIL: 10 >= 10 (out of bounds)  dtt-error {{out of bounds}}
}

int main() {
    test_struct_member_array();
    return 0;
}
