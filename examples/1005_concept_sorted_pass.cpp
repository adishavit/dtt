// Value Concepts: Predefined 'sorted' concept - passing cases
// When implemented, this should compile without errors
//
// sorted := forall(i, 0, _size - 1, _data[i] <= _data[i + 1])
// std::sort establishes 'sorted'

#include <vector>
#include <algorithm>

// Binary search requires sorted input
[[clang::annotate("dtt:requires=sorted")]]
bool binary_search_wrapper(const std::vector<int>& v, int target) {
    return std::binary_search(v.begin(), v.end(), target);
}

// Sort establishes sorted postcondition
// (This would be auto-inferred from std::sort in full implementation)
[[clang::annotate("dtt:ensures=sorted")]]
void sort_wrapper(std::vector<int>& v) {
    std::sort(v.begin(), v.end());
}

int main() {
    // Variant 1: Sort then search
    std::vector<int> v = {5, 2, 8, 1, 9};
    sort_wrapper(v);
    binary_search_wrapper(v, 5);  // Should pass: sort established 'sorted'

    // Variant 2: Already sorted literal
    std::vector<int> sorted_vec = {1, 2, 3, 4, 5};
    // With inference, this could be detected as sorted
    // For now, would need explicit annotation or sort call

    // Variant 3: Empty vector is vacuously sorted
    std::vector<int> empty_vec;
    // forall over empty range is true

    return 0;
}
