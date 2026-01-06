// Example 82: std::span OOB — should FAIL
// Accessing beyond template-specified bounds for static extent span

#include <span>

void test_oob() {
    int data[10] = {0};
    std::span<int, 10> sp(data);
    
    sp[15];  // ERROR: 15 >= 10
}

int main() {
    test_oob();
    return 0;
}
