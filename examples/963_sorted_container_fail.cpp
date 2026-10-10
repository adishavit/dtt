// 963_sorted_container_fail.cpp
// Tests: Sorted container tracking - binary_search on unsorted data
// From BLOG.md: Sorted Containers example

template<typename T>
struct SortedVec {
    [[clang::annotate("dtt:ensures=_size == 0")]]
    [[clang::annotate("dtt:ensures=_sorted == 1")]]
    SortedVec() {}

    [[clang::annotate("dtt:ensures=_sorted == 0")]]
    [[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
    void push_back(T val) {}

    [[clang::annotate("dtt:requires=_sorted == 1")]]
    bool binary_search(T val) { return false; }

    [[clang::annotate("dtt:ensures=_sorted == 1")]]
    void sort() {}
};

void test_binary_search_unsorted() {
    SortedVec<int> v;
    v.push_back(3);
    v.push_back(1);
    v.binary_search(2);    // ERROR: _sorted is 0, requires 1  dtt-error {{_sorted == 1}}
}

int main() {
    test_binary_search_unsorted();
    return 0;
}
