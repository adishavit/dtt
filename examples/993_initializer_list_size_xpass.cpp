// Safe initializer-list construction: known false positive.
// DTT does not track constructor-list size; assign({...}) is a separate path.

#include <vector>

// Gap: Initializer list should set _size but DTT doesn't track it
void test_initializer_list_size() {
    std::vector<int> v = {1, 2, 3};  // Should set _size = 3
    v[0];   // Should be OK: index 0 < size 3
    v[2];   // Should be OK: index 2 < size 3
}

// Gap: Even with more elements
void test_larger_initializer_list() {
    std::vector<int> v = {1, 2, 3, 4, 5};  // Should set _size = 5
    v[4];   // Should be OK: index 4 < size 5
}

int main() { return 0; }
