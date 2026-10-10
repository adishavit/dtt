// Value Concepts: User-defined concepts with dtt:define
// This test documents the gap: DTT should catch these but doesn't yet
//
// Syntax: [[clang::annotate("dtt:define=name := expr")]]
//
// When implemented:
// - Rename to 1006_concept_define_fail.cpp

#include <vector>

// Define composite concept: searchable := nonempty && sorted
[[clang::annotate("dtt:define=searchable := nonempty && sorted")]]

// Function using user-defined concept
[[clang::annotate("dtt:requires=searchable")]]
int safe_binary_search(const std::vector<int>& v, int target) {
    // Can safely use binary search: nonempty && sorted proven
    size_t lo = 0, hi = v.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (v[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return (lo < v.size() && v[lo] == target) ? (int)lo : -1;
}

int main() {
    // Variant 1: Empty vector (fails nonempty part of searchable)
    std::vector<int> empty_vec;
    safe_binary_search(empty_vec, 42);  // SHOULD FAIL: not nonempty  dtt-error {{_size > 0 \&\& _sorted == 1}}

    // Variant 2: Non-empty but unsorted (fails sorted part)
    std::vector<int> unsorted = {5, 2, 8};
    safe_binary_search(unsorted, 5);  // SHOULD FAIL: not sorted  dtt-error {{_size > 0 \&\& _sorted == 1}}

    // Variant 3: Sorted but then modified
    std::vector<int> v = {1, 2, 3};
    // sort(v) would establish sorted
    v.push_back(0);  // Breaks sorted
    safe_binary_search(v, 2);  // SHOULD FAIL: no longer sorted  dtt-error {{_size > 0 \&\& _sorted == 1}}

    return 0;
}
