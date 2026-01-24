// Example 52: Loop bounds tracking — should COMPILE
// DTT tracks loop variable bounds: for (int i = 0; i < v.size(); i++)

#include <vector>

void test_basic_loop() {
    std::vector<int> v;
    v.resize(10);  // _size = 10

    // Loop with proper bounds - DTT proves i < _size inside loop
    for (int i = 0; i < v.size(); i++) {
        v[i] = i * 2;  // OK: i < _size proven by loop condition
    }
}

void test_nested_loop() {
    std::vector<int> v;
    v.resize(5);

    for (int i = 0; i < v.size(); i++) {
        for (int j = 0; j < v.size(); j++) {
            // Both i and j are proven < _size
            v[i] = v[j];  // OK
        }
    }
}

int main() {
    test_basic_loop();
    test_nested_loop();
    return 0;
}
