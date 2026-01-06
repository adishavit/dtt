// Example 77: std::array bounds checking — should COMPILE
// DTT extracts size from template parameter and checks operator[]

#include <array>
#include <cstddef>

void test_basic_access() {
    std::array<int, 10> arr;
    
    arr[0];  // OK: 0 < 10
    arr[5];  // OK: 5 < 10
    arr[9];  // OK: 9 < 10
}

void test_different_sizes() {
    std::array<double, 3> small;
    std::array<char, 100> large;
    
    small[2];  // OK: 2 < 3
    large[99]; // OK: 99 < 100
}

void test_nested() {
    std::array<std::array<int, 3>, 5> matrix;
    
    matrix[4];     // OK: 4 < 5 (outer)
    matrix[0][2];  // OK: 0 < 5, 2 < 3
}

int main() {
    test_basic_access();
    test_different_sizes();
    test_nested();
    return 0;
}
