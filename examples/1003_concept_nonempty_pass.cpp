// Value Concepts: Predefined 'nonempty' concept - passing cases
// When implemented, this should compile without errors
//
// nonempty := _size > 0

#include <vector>

// Using predefined concept 'nonempty'
[[clang::annotate("dtt:requires=nonempty")]]
int get_first(std::vector<int>& v) {
    return v.front();
}

int main() {
    // Variant 1: After push_back, vector is nonempty
    std::vector<int> v;
    v.push_back(42);
    get_first(v);  // Should pass: v has 1 element

    // Variant 2: Initialized with elements
    std::vector<int> v2 = {1, 2, 3};
    get_first(v2);  // Should pass: v2 has 3 elements

    // Variant 3: After resize to non-zero
    std::vector<int> v3;
    v3.resize(5);
    get_first(v3);  // Should pass: v3 has 5 elements

    // Variant 4: Guard check on potentially non-empty vector
    std::vector<int> v4;
    v4.push_back(1);  // v4._size = 1
    v4.pop_back();    // v4._size = 0
    v4.push_back(2);  // v4._size = 1 again
    if (!v4.empty()) {
        get_first(v4);  // Should pass: guard proves nonempty
    }

    return 0;
}
