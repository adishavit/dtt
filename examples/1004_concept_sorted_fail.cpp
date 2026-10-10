// Value Concepts: Predefined 'sorted' concept
// This test documents the gap: DTT should catch these but doesn't yet
//
// sorted := forall(i, 0, _size - 1, _data[i] <= _data[i + 1])
//
// When implemented:
// - Rename to 1004_concept_sorted_fail.cpp

#include <vector>
#include <algorithm>

// Binary search requires sorted input
[[clang::annotate("dtt:requires=sorted")]]
bool binary_search_wrapper(const std::vector<int>& v, int target) {
    return std::binary_search(v.begin(), v.end(), target);
}

// Lower bound requires sorted
[[clang::annotate("dtt:requires=sorted")]]
int lower_bound_wrapper(const std::vector<int>& v, int target) {
    auto it = std::lower_bound(v.begin(), v.end(), target);
    return (it != v.end()) ? *it : -1;
}

int main() {
    // Variant 1: Unsorted vector
    std::vector<int> unsorted = {5, 2, 8, 1, 9};
    binary_search_wrapper(unsorted, 5);  // SHOULD FAIL: not sorted  dtt-error {{_sorted == 1}}

    // Variant 2: After push_back (may break sorting)
    std::vector<int> v = {1, 2, 3};
    // std::sort(v.begin(), v.end());  // Would establish sorted
    v.push_back(0);  // Appends 0, breaking sorted order
    lower_bound_wrapper(v, 2);  // SHOULD FAIL: push_back may break sorted  dtt-error {{_sorted == 1}}

    // Variant 3: Reverse sorted (wrong order)
    std::vector<int> reverse_sorted = {9, 7, 5, 3, 1};
    binary_search_wrapper(reverse_sorted, 5);  // SHOULD FAIL: descending, not ascending  dtt-error {{_sorted == 1}}

    return 0;
}
