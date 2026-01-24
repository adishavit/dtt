// Example 97: Division for midpoint — should COMPILE
// Tests: mid = size / 2 where size >= 2 => mid >= 1

#include <vector>

void test() {
    std::vector<int> v;
    v.resize(10);  // _size = 10
    
    // Calculate midpoint
    size_t mid = v.size() / 2;  // mid = 5
    
    v[mid];  // OK: 5 < 10
    v[mid - 1];  // OK: 4 < 10
}

int main() {
    test();
    return 0;
}
