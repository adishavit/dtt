// Example 87: std::array with for loop — should COMPILE
// Combines template param extraction with loop bounds

#include <array>

void test() {
    std::array<int, 10> arr;
    
    for (int i = 0; i < 10; i++) {
        arr[i] = i * 2;  // OK: i < 10 = arr._size
    }
}

int main() {
    test();
    return 0;
}
