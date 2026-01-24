// Example 58: Index arithmetic failure — should NOT compile
// Tests: computed index that exceeds bounds

#include <vector>

void test_bad_index() {
    std::vector<int> v;
    v.resize(10);  // _size = 10
    
    int idx = 15;   // Out of bounds!
    v[idx];         // ERROR: 15 >= 10
}

int main() {
    test_bad_index();
    return 0;
}
