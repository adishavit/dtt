// Example 466: Address-of single variable — should COMPILE
// Tests that &var is tracked as 1-element buffer

void test_address_of_single() {
    int a = 42;
    int* p = &a;  // p points to single int

    p[0] = 1;     // OK: first (and only) element
    *p = 2;       // OK: same as p[0]
}

void test_address_of_in_array() {
    int arr[3] = {1, 2, 3};
    int* p = &arr[1];  // p points to middle element

    p[0] = 10;    // OK: arr[1]
    p[-1] = 20;   // OK: arr[0] (interior pointer)
    p[1] = 30;    // OK: arr[2]
}

int main() {
    test_address_of_single();
    test_address_of_in_array();
    return 0;
}
