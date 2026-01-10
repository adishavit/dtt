// Example 408: Buffer pointer from new[] — should COMPILE
// Tests tracking bounds from new[] expressions

void test_new_array_literal() {
    int* p = new int[10];  // p: _size = 10

    p[0] = 1;              // OK: 0 < 10
    p[9] = 2;              // OK: 9 < 10

    delete[] p;
}

void test_new_array_const() {
    const int N = 5;
    int* p = new int[N];   // p: _size = N = 5

    p[4] = 1;              // OK: 4 < 5

    delete[] p;
}

int main() {
    test_new_array_literal();
    test_new_array_const();
    return 0;
}
