// Example 59: Return size relationships — should COMPILE
// Tests: result._size = arg0._size + arg1._size

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

void test_concat_size() {
    std::vector<int> x;
    x.resize(3);  // x._size = 3
    
    std::vector<int> y;
    y.resize(4);  // y._size = 4
    
    std::vector<int> z = concat(x, y);  // z._size = 3 + 4 = 7
    
    // Now we should be able to access indices 0-6
    z[0];  // OK
    z[6];  // OK: 6 < 7
}

int main() {
    test_concat_size();
    return 0;
}
