// Example 421: Buffer pointer requires annotation — should PASS
// Tests that requires=buf._size >= n is satisfied at call site

#include <cstddef>

[[clang::annotate("dtt:requires=buf._size >= n")]]
void process(int* buf, size_t n) {
    // Function body - assumes buf has at least n elements
}

void test_requires_satisfied() {
    int arr[10];
    process(arr, 5);   // OK: arr._size=10 >= 5
    process(arr, 10);  // OK: arr._size=10 >= 10
}

int main() {
    test_requires_satisfied();
    return 0;
}
