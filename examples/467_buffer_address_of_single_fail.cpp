// Example 467: Address-of single variable OOB — should FAIL
// Tests that p[1] on &single_var is caught as OOB

void test_address_of_oob() {
    int a = 42;
    int* p = &a;  // p points to single int, _size = 1

    p[1] = 1;     // FAIL: only p[0] is valid
}

void test_address_of_negative() {
    int a = 42;
    int* p = &a;

    p[-1] = 1;    // FAIL: negative index on single element
}

int main() {
    test_address_of_oob();
    test_address_of_negative();
    return 0;
}
