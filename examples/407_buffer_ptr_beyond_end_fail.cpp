// Example 407: Buffer pointer beyond one-past-end — should FAIL
// Creating pointer more than one past end is UB per C++ standard

void test_beyond_end() {
    int arr[10];
    int* p = arr + 11;     // ERROR: UB - beyond one-past-end  dtt-error {{pointer arithmetic creates out-of-bounds pointer}}
}

int main() {
    test_beyond_end();
    return 0;
}
