// Example 47: Index bounds checking — should COMPILE

#include <vector>

int main() {
    std::vector<int> v;
    
    v.push_back(1);  // _size = 1
    v.push_back(2);  // _size = 2
    v.push_back(3);  // _size = 3
    
    v[0] = 10;  // OK: 0 < 3
    v[1] = 20;  // OK: 1 < 3
    v[2] = 30;  // OK: 2 < 3
    
    return 0;
}
