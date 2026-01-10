// Example 481: Pointer increment OOB — should FAIL
// Tests that p++ past end is caught

void test_increment_oob() {
    int arr[3];
    int* p = arr;
    p++;           // offset 1
    p++;           // offset 2
    p++;           // offset 3 (past end!)
    *p = 0;        // FAIL: offset 3 >= size 3
}

void test_preincrement_oob() {
    int arr[2];
    int* p = arr;
    ++p;           // offset 1
    ++p;           // offset 2 (past end!)
    *p = 0;        // FAIL: offset 2 >= size 2
}

void test_decrement_oob() {
    int arr[5];
    int* p = arr + 2;  // offset=2 into arr
    --p;               // offset 1
    --p;               // offset 0
    --p;               // offset -1 (before start!)
    *p = 0;            // FAIL: negative offset
}

int main() {
    test_increment_oob();
    test_preincrement_oob();
    test_decrement_oob();
    return 0;
}
