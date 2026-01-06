// Example 80: std::bitset OOB — should FAIL
// Accessing beyond template-specified bounds

#include <bitset>

void test_oob() {
    std::bitset<8> byte;
    
    byte[10];  // ERROR: 10 >= 8
}

int main() {
    test_oob();
    return 0;
}
