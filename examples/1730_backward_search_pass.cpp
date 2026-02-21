// Test: Safe code should NOT trigger backward search notes
// When preconditions are satisfied, no backward search suggestions should appear.

#include <vector>
#include <optional>

// Variant 1: Non-empty vector — no backward search needed
void variant1_nonempty() {
    std::vector<int> v;
    v.push_back(1);
    int x = v.front();  // OK: _size = 1 > 0
}

// Variant 2: Optional with value — no backward search needed
void variant2_has_value() {
    std::optional<int> opt = 42;
    int x = opt.value();  // OK: _has_value = 1
}

// Variant 3: Guarded access — no backward search needed
void variant3_guarded() {
    std::vector<int> v;
    if (!v.empty()) {
        int x = v.front();  // OK: guard proves _size > 0
    }
}

// Variant 4: Re-established after clear — no backward search needed
void variant4_reestablished() {
    std::vector<int> v(5);
    v.clear();
    v.resize(10);
    int x = v[5];  // OK: _size = 10, 5 < 10
}
