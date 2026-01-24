// Example 60: Return size relationships failure — should NOT compile
// Tests: accessing beyond combined size

#include <vector>

// A function that returns a combined-size container
[[clang::annotate("dtt:ensures=result._size == arg0._size + arg1._size")]]
std::vector<int> concat(const std::vector<int>& a, const std::vector<int>& b) {
    std::vector<int> result;
    result.reserve(a.size() + b.size());
    for (size_t i = 0; i < a.size(); i++) result.push_back(a[i]);
    for (size_t i = 0; i < b.size(); i++) result.push_back(b[i]);
    return result;
}

void test_concat_bounds() {
    std::vector<int> x;
    x.resize(3);  // x._size = 3
    
    std::vector<int> y;
    y.resize(4);  // y._size = 4
    
    std::vector<int> z = concat(x, y);  // z._size = 3 + 4 = 7
    
    z[7];  // ERROR: 7 >= 7 (should be < 7)
}

int main() {
    test_concat_bounds();
    return 0;
}
