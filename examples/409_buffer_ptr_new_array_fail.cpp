// Example 409: Buffer pointer from new[] OOB — should FAIL
// Tests that OOB access on new[] allocated buffer is caught

void test_new_array_oob() {
    int* p = new int[10];  // p: _size = 10

    p[10] = 1;             // ERROR: 10 >= 10

    delete[] p;
}

int main() {
    test_new_array_oob();
    return 0;
}
