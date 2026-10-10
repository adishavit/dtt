// Example 46: std::vector pop_back on empty — should FAIL

#include <vector>

int main() {
    std::vector<int> v;
    
    v.pop_back();  // ERROR: _size = 0, pop_back requires _size > 0  dtt-error {{_size > 0}}
    
    return 0;
}
