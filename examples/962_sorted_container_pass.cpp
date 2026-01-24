// 962_sorted_container_pass.cpp
// Tests: Sorted container tracking - binary_search only on sorted data
// From BLOG.md: Sorted Containers example

template<typename T>
struct SortedVec {
    [[clang::annotate("dtt:ensures=_size == 0")]]
    [[clang::annotate("dtt:ensures=_sorted == 1")]]
    SortedVec() {}

    [[clang::annotate("dtt:ensures=_sorted == 1")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void insert_sorted(T val) {}

    [[clang::annotate("dtt:ensures=_sorted == 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(T val) {}

    [[clang::annotate("dtt:requires=_sorted == 1")]]
    bool binary_search(T val) { return false; }

    [[clang::annotate("dtt:ensures=_sorted == 1")]]
    void sort() {}
};

// Variant 1: insert_sorted maintains sorted property
void test_insert_sorted() {
    SortedVec<int> v;
    v.insert_sorted(3);
    v.insert_sorted(1);
    v.insert_sorted(2);
    v.binary_search(2);    // OK: _sorted == 1
}

// Variant 2: push_back breaks sorted, sort() restores it
void test_sort_after_push() {
    SortedVec<int> v;
    v.push_back(3);
    v.push_back(1);
    // _sorted == 0 here
    v.sort();              // _sorted = 1
    v.binary_search(2);    // OK: _sorted == 1
}

// Variant 3: Empty container is sorted
void test_empty_sorted() {
    SortedVec<int> v;      // _sorted = 1 (empty is sorted)
    v.binary_search(42);   // OK
}

int main() {
    test_insert_sorted();
    test_sort_after_push();
    test_empty_sorted();
    return 0;
}
