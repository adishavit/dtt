// 1214_template_edge_cases_pass.cpp
// Tests: Edge cases for template instantiation analysis — should compile clean
//
// Exercises: empty packs, nested namespaces, extern "C++", SFINAE,
// recursive templates, and other edge cases that could trip up Pass 2b.

#include <vector>

// =============================================================================
// SECTION 1: Empty parameter pack
// =============================================================================

// Variant 1.1: Zero-arg call — fold expands to nothing, no push_backs
template<typename... Args>
void empty_fold(Args... args) {
    std::vector<int> v;
    (v.push_back(args), ...);
    // No v[0] here — _size = 0 for empty pack, access would be a bug
    (void)v;
}

void variant1_empty_pack() {
    empty_fold();  // 0 args → 0 push_backs
}

// =============================================================================
// SECTION 2: Nested namespace containing function template
// =============================================================================

namespace outer {
namespace inner {
    template<typename... Args>
    void nested_push(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK: _size = N
    }
}  // namespace inner
}  // namespace outer

void variant2_nested_namespace() {
    outer::inner::nested_push(1, 2);
}

// =============================================================================
// SECTION 3: extern "C++" block containing function template
// =============================================================================

extern "C++" {
    template<typename... Args>
    void extern_push(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK
    }
}

void variant3_extern_cpp() {
    extern_push(1, 2, 3);
}

// =============================================================================
// SECTION 4: Non-variadic function template (not a fold, but still a
// template specialization that Pass 2b should handle)
// =============================================================================

template<typename T>
void single_param_push(T val) {
    std::vector<int> v;
    v.push_back(static_cast<int>(val));
    v[0];  // OK: _size = 1
}

void variant4_non_variadic() {
    single_param_push(42);
    single_param_push(3.14);
}

// =============================================================================
// SECTION 5: Multiple function templates in the same struct
// =============================================================================

struct MultiMethod {
    template<typename... Args>
    void alpha(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK
    }

    template<typename... Args>
    void beta(Args... args) {
        std::vector<int> v;
        (v.push_back(args), ...);
        v[0];  // OK
    }
};

void variant5_multi_method() {
    MultiMethod m;
    m.alpha(1, 2);
    m.beta(3, 4, 5);
}

int main() {
    variant1_empty_pack();
    variant2_nested_namespace();
    variant3_extern_cpp();
    variant4_non_variadic();
    variant5_multi_method();
    return 0;
}
