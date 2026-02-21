// Test: Safe code should NOT trigger provenance chain diagnostics
// DTT should emit no errors and no provenance notes when preconditions
// are satisfied, even after multiple state transitions.

#include <vector>

// Variant 1: Multiple state changes but safe final access
void variant1_safe_after_transitions() {
    std::vector<int> v;       // _size = 0
    v.push_back(1);           // _size = 1
    v.push_back(2);           // _size = 2
    v.clear();                // _size = 0
    v.push_back(3);           // _size = 1
    int x = v.front();        // OK: _size = 1 > 0, no provenance notes
}

// Variant 2: Resize then access within bounds
void variant2_resize_safe() {
    std::vector<int> v;
    v.resize(10);             // _size = 10
    v.clear();                // _size = 0
    v.resize(5);              // _size = 5
    int x = v[4];             // OK: 4 < 5, no provenance notes
}

// Variant 3: Guard-protected access after clear
void variant3_guarded_after_clear() {
    std::vector<int> v;
    v.push_back(1);
    v.clear();
    v.push_back(42);
    if (!v.empty()) {
        int x = v.front();   // OK: guard proves _size > 0
    }
}

// Variant 4: Pop to non-empty is safe
void variant4_pop_still_nonempty() {
    std::vector<int> v(5);    // _size = 5
    v.pop_back();             // _size = 4
    v.pop_back();             // _size = 3
    int x = v.front();        // OK: _size = 3 > 0
}
