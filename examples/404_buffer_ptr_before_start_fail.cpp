// Example 404: Buffer pointer before start — should FAIL
// Creating a pointer before the array start is UB per C++ standard

void test_before_start() {
    int arr[10];
    int* p = arr - 1;   // ERROR: UB - pointer before array start  dtt-error {{pointer arithmetic creates out-of-bounds pointer}}
}

int main() {
    test_before_start();
    return 0;
}
