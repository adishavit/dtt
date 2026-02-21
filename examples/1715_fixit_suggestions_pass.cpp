// Test: Safe code should NOT trigger FixItHint suggestions
// DTT should emit no errors and no fix-it suggestions when preconditions
// are properly satisfied.

#include <vector>
#include <optional>

// Variant 1: Non-empty container access — _size > 0 proven
void variant1_nonempty_container() {
    std::vector<int> v;
    v.push_back(1);
    int x = v.front();  // OK: _size = 1 > 0, no fix-it needed
}

// Variant 2: Optional with value — _has_value proven
void variant2_optional_with_value() {
    std::optional<int> opt = 42;
    int x = opt.value();  // OK: _has_value = 1, no fix-it needed
}

// Variant 3: In-bounds index access — arg0 < _size proven
void variant3_index_in_bounds() {
    std::vector<int> v(10);
    int x = v[5];  // OK: 5 < 10, no fix-it needed
}

// Variant 4: Non-null pointer dereference — _nonnull proven
void variant4_nonnull_deref() {
    int val = 42;
    int* p = &val;
    int x = *p;  // OK: p is non-null (address-of), no fix-it needed
}

// Variant 5: Guarded empty check — safe after guard
void variant5_guarded_empty_check() {
    std::vector<int> v;
    if (!v.empty()) {
        int x = v.front();  // OK: guard proves _size > 0
    }
}

// Variant 6: Guarded optional check — safe after has_value guard
void variant6_guarded_optional() {
    std::optional<int> opt;
    if (opt.has_value()) {
        int x = opt.value();  // OK: guard proves _has_value == 1
    }
}

// Variant 7: Guarded null check — safe after pointer guard
void variant7_guarded_null_check() {
    int* p = nullptr;
    if (p) {
        int x = *p;  // OK: guard proves _nonnull != 0
    }
}
