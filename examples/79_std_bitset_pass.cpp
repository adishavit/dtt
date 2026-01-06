// Example 79: std::bitset bounds checking — should COMPILE
// DTT extracts size from template parameter and checks operator[]

#include <bitset>

void test_basic() {
    std::bitset<8> byte;
    
    byte[0];  // OK: 0 < 8
    byte[7];  // OK: 7 < 8
}

void test_large() {
    std::bitset<64> qword;
    
    qword[63];  // OK: 63 < 64
}

int main() {
    test_basic();
    test_large();
    return 0;
}
