// Example 412: Buffer pointer type casting OOB — should FAIL
// Tests that casting preserves bounds and catches OOB

void test_cast_oob() {
    int arr[10];           // 40 bytes
    char* c = (char*)arr;  // _size=40 chars

    c[40] = 1;             // ERROR: 40 >= 40
}

int main() {
    test_cast_oob();
    return 0;
}
