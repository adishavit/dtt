// Example 92: Postcondition decrement OOB — should FAIL
// Tests that decremented size is tracked correctly

#include <vector>

void test() {
    std::vector<int> v;
    v.resize(5);  // _size = 5
    
    v.pop_back();  // _size = 4
    v.pop_back();  // _size = 3
    v.pop_back();  // _size = 2
    
    v[2];  // ERROR: 2 >= 2 (not < 2)
}

int main() {
    test();
    return 0;
}
