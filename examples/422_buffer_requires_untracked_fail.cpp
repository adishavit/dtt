// Example 422: Buffer pointer requires with untracked pointer — should FAIL
// Tests that untracked buffer produces Category C warning

#include <cstddef>

int* get_buffer();

[[clang::annotate("dtt:requires=buf._size >= n")]]
void process(int* buf, size_t n) {
    // Function body
}

void test_untracked_buffer() {
    int* unknown = get_buffer();  // Not tracked
    process(unknown, 10);         // ERROR: unknown._size is unknown (Category C)  dtt-error {{arg0\._size >= arg1}}
}

int main() {
    test_untracked_buffer();
    return 0;
}
