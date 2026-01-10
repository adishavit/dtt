// Example 405: Buffer pointer one-past-end — should COMPILE
// Creating one-past-end pointer is valid (for iterator idioms)

void test_one_past_end_creation() {
    int arr[10];
    int* end = arr + 10;   // OK: one-past-end is valid to create

    // Using it for iteration
    for (int* p = arr; p != end; ++p) {
        *p = 0;            // OK: p is always < end in this loop
    }
}

int main() {
    test_one_past_end_creation();
    return 0;
}
