// Example 440: Struct member array valid access — should COMPILE
// Tests that valid accesses within struct array bounds are allowed

struct Buffer {
    int data[10];
};

void test_valid_access() {
    Buffer buf;
    int* p = buf.data;     // _size=10

    p[0] = 1;              // OK: first element
    p[9] = 2;              // OK: last element
}

struct Large {
    double values[100];
};

void test_large_array() {
    Large l;
    double* p = l.values;  // _size=100

    p[50] = 3.14;          // OK: middle
    p[99] = 2.71;          // OK: last
}

int main() {
    test_valid_access();
    test_large_array();
    return 0;
}
