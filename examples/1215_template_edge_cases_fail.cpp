// 1215_template_edge_cases_fail.cpp
// Tests: Edge cases that DTT should catch — must produce errors
//
// Various structural contexts where function template specializations
// have out-of-bounds accesses that Pass 2b should detect.

#include <vector>

// =============================================================================
// SECTION 1: Nested namespace — empty vector access in template
// =============================================================================

namespace deep {
namespace nested {
    template<typename... Args>
    void bad_access(Args...) {
        std::vector<int> v;
        v[0];  // ERROR: _size = 0  dtt-error {{out of bounds}}
    }
}  // namespace nested
}  // namespace deep

void variant1_nested_ns() {
    deep::nested::bad_access(1);
}

// =============================================================================
// SECTION 2: Non-variadic template — empty access
// =============================================================================

template<typename T>
void empty_access(T) {
    std::vector<int> v;
    v[0];  // ERROR: _size = 0  dtt-error {{out of bounds}}
}

void variant2_non_variadic() {
    empty_access(42);
}

int main() {
    variant1_nested_ns();
    variant2_non_variadic();
    return 0;
}
