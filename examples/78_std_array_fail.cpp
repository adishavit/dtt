// Example 78: std::array OOB — should FAIL
// Accessing beyond template-specified bounds

#include <array>

void test_oob() {
    std::array<int, 5> arr;
    
    arr[10];  // ERROR: 10 >= 5
}

int main() {
    test_oob();
    return 0;
}
