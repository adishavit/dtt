// Example 81: std::span static extent bounds checking — should COMPILE
// DTT extracts size from template parameter for static extent spans

#include <span>
#include <array>

void test_static_span() {
    int data[10] = {0};
    std::span<int, 10> sp(data);
    
    sp[0];  // OK: 0 < 10
    sp[9];  // OK: 9 < 10
}

void test_from_array() {
    std::array<int, 5> arr;
    std::span<int, 5> sp(arr);
    
    sp[4];  // OK: 4 < 5
}

void test_small_span() {
    int data[3] = {1, 2, 3};
    std::span<int, 3> sp(data);
    
    sp[0];  // OK
    sp[2];  // OK: 2 < 3
}

int main() {
    test_static_span();
    test_from_array();
    test_small_span();
    return 0;
}
