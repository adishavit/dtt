// Example 45: std::vector usage — should COMPILE

#include <vector>

int main() {
    std::vector<int> v;
    
    v.push_back(1);  // _size = 1
    v.push_back(2);  // _size = 2
    v.push_back(3);  // _size = 3
    
    if (!v.empty()) {
        v.front();   // OK: !empty() proves _size > 0
        v.back();    // OK
        v.pop_back();  // OK
    }
    
    return 0;
}
