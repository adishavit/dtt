// Value Concepts: Predefined 'nonempty' concept
// DTT now expands 'nonempty' to '_size > 0' and catches violations
//
// nonempty := _size > 0

#include <vector>

// Using predefined concept 'nonempty' instead of _size > 0
[[clang::annotate("dtt:requires=nonempty")]]
int get_first(std::vector<int>& v) {
    return v.front();
}

// Chained concept
[[clang::annotate("dtt:requires=nonempty")]]
[[clang::annotate("dtt:ensures=nonempty")]]
int pop_and_return(std::vector<int>& v) {
    int val = v.back();
    v.pop_back();
    return val;
}

int main() {
    std::vector<int> empty_vec;

    // Variant 1: Call on empty vector
    get_first(empty_vec);  // SHOULD FAIL: empty_vec is empty, not nonempty

    // Variant 2: After clear
    std::vector<int> v = {1, 2, 3};
    v.clear();
    get_first(v);  // SHOULD FAIL: v was cleared, now empty

    return 0;
}
