// Example 51: Symbolic argument tracking fail — should FAIL

#include <vector>

int main() {
    std::vector<int> v;
    v.resize(3);   // _size = 3
    v[3] = 42;     // ERROR: 3 < 3 is false  dtt-error {{out of bounds}}
    return 0;
}
