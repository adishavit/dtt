// 1213_template_spec_state_isolation_pass.cpp
// Tests: Multiple template specializations maintain isolated state
//
// When Pass 2b traverses multiple specializations of the same function
// template, each specialization must start with clean per-function state.
// State from one specialization must NOT leak into the next.

#include <vector>

// =============================================================================
// SECTION 1: Free function template — multiple specializations
// =============================================================================

// Variant 1.1: Different pack sizes, each does its own push_back
template<typename... Args>
void fold_push_isolated(Args... args) {
    std::vector<int> v;
    (v.push_back(args), ...);
    v[0];  // OK: each specialization has its own _size
}

void variant1_isolated_specs() {
    fold_push_isolated(1);            // _size = 1 → OK
    fold_push_isolated(1, 2, 3);      // _size = 3 → OK
    fold_push_isolated(1, 2, 3, 4, 5);  // _size = 5 → OK
}

// =============================================================================
// SECTION 2: Method template — multiple specializations
// =============================================================================

struct Builder {
    template<typename... Args>
    void add(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK: isolated per specialization
    }
};

void variant2_method_isolated() {
    Builder b;
    b.add(1);
    b.add(1, 2);
    b.add(1, 2, 3, 4);
}

int main() {
    variant1_isolated_specs();
    variant2_method_isolated();
    return 0;
}
