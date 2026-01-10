// Example 443: Struct member array out of bounds — should FAIL
// Tests various OOB scenarios with struct member arrays

struct Small {
    int data[4];
};

void test_oob() {
    Small s;
    int* p = s.data;       // _size=4

    p[4] = 1;              // FAIL: 4 >= 4 (one past end)
}

int main() {
    test_oob();
    return 0;
}
