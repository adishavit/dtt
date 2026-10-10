// Test: FixItHint suggestions for common precondition patterns
// DTT should emit fix-it suggestions alongside errors for known patterns:
//   - _size > 0     → fix-it: "if (!v.empty()) { ... }"
//   - _nonnull != 0 → fix-it: "if (ptr) { ... }"
//   - _has_value != 0 → fix-it: "if (opt.has_value()) { ... }"
//   - arg0 < _size  → fix-it: "if (idx < v.size()) { ... }"
//
// This test produces DTT errors — each variant exercises a different
// FixItHint pattern.

#include <vector>
#include <optional>

// Variant 1: empty container access — _size > 0 not satisfied
void variant1_empty_container() {
    std::vector<int> v;
    int x = v.front();  // ERROR + fix-it: if (!v.empty()) { ... }  dtt-error {{_size > 0}}
}

// Variant 2: optional access without check — _has_value != 0
void variant2_optional_no_check() {
    std::optional<int> opt;
    int x = opt.value();  // ERROR + fix-it: if (opt.has_value()) { ... }  dtt-error {{_has_value == 1}}
}

// Variant 3: out-of-bounds index — arg0 < _size not satisfied
void variant3_index_oob() {
    std::vector<int> v(3);
    int x = v[5];  // ERROR + fix-it: if (idx < v.size()) { ... }  dtt-error {{out of bounds}}
}

// Variant 4: null pointer dereference — _nonnull not proven
void variant4_null_deref() {
    int* p = nullptr;
    int x = *p;  // ERROR + fix-it: if (p) { ... }  dtt-error {{potential null pointer dereference}}
}
