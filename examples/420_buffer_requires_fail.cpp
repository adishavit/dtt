// Example 420: Buffer pointer requires annotation — should FAIL
// Tests that requires=buf._size >= n is enforced at call site
// Caller passes buffer that's too small

#include <cstddef>

[[clang::annotate("dtt:requires=buf._size >= n")]]
void process(int* buf, size_t n) {
    // Function body - assumes buf has at least n elements
}

void test_requires_violation() {
    int arr[10];
    process(arr, 15);  // ERROR: arr._size=10 < 15  dtt-error {{arg0\._size >= arg1}}
}

int main() {
    test_requires_violation();
    return 0;
}
