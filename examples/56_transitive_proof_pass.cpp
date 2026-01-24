// Example 56: Transitive proofs — should COMPILE
// If _size == n and n > 0, then _size > 0

#include <vector>

void test_resize_guarded(size_t n) {
    std::vector<int> v;
    v.resize(n);       // _size = n (symbolic)
    
    if (n > 0) {
        // n > 0 is proven here
        // Since _size == n, we should be able to prove _size > 0
        v.front();     // Should be OK via transitive proof
    }
}

void test_assign_guarded(size_t count) {
    std::vector<int> v;
    v.assign(count, 42);   // _size = count
    
    if (count > 0) {
        v.pop_back();      // Should be OK: _size == count > 0
    }
}

int main() {
    test_resize_guarded(10);
    test_assign_guarded(5);
    return 0;
}
