// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 805_std_optional_fail.cpp
// Tests: Unsafe std::optional usage that DTT SHOULD catch but doesn't yet

#include <optional>
#include <string>

// =============================================================================
// SECTION 1: Access without guard
// =============================================================================

// Variant 1.1: value() on default-constructed optional
void test_value_empty() {
    std::optional<int> opt;
    opt.value();  // ERROR: _has_value unknown (throws at runtime)  // dtt-error {{_has_value}}
}

// Variant 1.2: operator* on empty optional
void test_deref_empty() {
    std::optional<int> opt;
    *opt;  // ERROR: _has_value == false (UB)  // dtt-error {{_has_value}}
}

// Variant 1.3: operator-> on empty optional
void test_arrow_empty() {
    struct S { int x; };
    std::optional<S> opt;
    opt->x;  // ERROR: _has_value == false (UB)  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 2: Access after reset()
// =============================================================================

// Variant 2.1: value() after reset()
void test_value_after_reset() {
    std::optional<int> opt = 42;
    opt.reset();   // _has_value = false
    opt.value();   // ERROR  // dtt-error {{_has_value}}
}

// Variant 2.2: operator* after reset()
void test_deref_after_reset() {
    std::optional<int> opt = 42;
    opt.reset();
    *opt;  // ERROR  // dtt-error {{_has_value}}
}

// Variant 2.3: operator-> after reset()
void test_arrow_after_reset() {
    struct S { int x; };
    std::optional<S> opt = S{42};
    opt.reset();
    opt->x;  // ERROR  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 3: Access after nullopt assignment
// =============================================================================

// Variant 3.1: value() after = std::nullopt
void test_value_after_nullopt() {
    std::optional<int> opt = 42;
    opt = std::nullopt;  // _has_value = false
    opt.value();         // ERROR  // dtt-error {{_has_value}}
}

// Variant 3.2: operator* after = std::nullopt
void test_deref_after_nullopt() {
    std::optional<int> opt = 42;
    opt = std::nullopt;
    *opt;  // ERROR  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 4: Access on parameter optional
// =============================================================================

// Variant 4.1: value() on parameter optional
void test_param_value(std::optional<int> opt) {
    opt.value();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove _has_value
}

// Variant 4.2: operator* on parameter optional
void test_param_deref(std::optional<int>& opt) {
    *opt;  // ERROR: can't prove _has_value  // dtt-error {{_has_value}}
}

// Variant 4.3: operator-> on parameter optional
void test_param_arrow(std::optional<std::string>& opt) {
    opt->size();  // ERROR: can't prove _has_value  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 5: Wrong branch access
// =============================================================================

// Variant 5.1: access in else of has_value()
void test_wrong_branch() {
    std::optional<int> opt;
    if (!opt.has_value()) {
        opt.value();  // ERROR: has_value is false here  // dtt-error {{_has_value}}
    }
}

// Variant 5.2: access in if(empty)
void test_empty_branch() {
    std::optional<int> opt;
    if (!opt) {
        *opt;  // ERROR: opt is empty here  // dtt-error {{_has_value}}
    }
}

// Variant 5.3: access outside guard scope
void test_outside_guard() {
    std::optional<int> opt = 42;
    if (opt) {
        // OK here
    }
    opt.reset();
    opt.value();  // ERROR: guard doesn't protect this  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 6: swap() loses tracking
// =============================================================================

// Variant 6.1: swap with unknown optional
void test_swap_unknown(std::optional<int>& other) {
    std::optional<int> opt = 42;
    opt.swap(other);  // opt now has unknown has_value
    opt.value();      // ERROR: can't prove _has_value  // dtt-error {{_has_value}}
}

// Variant 6.2: swap with empty optional
void test_swap_empty() {
    std::optional<int> opt1 = 42;
    std::optional<int> opt2;  // empty
    opt1.swap(opt2);
    opt1.value();  // ERROR: opt1 is now empty  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 7: Copy/move from empty
// =============================================================================

// Variant 7.1: copy from empty optional
void test_copy_empty() {
    std::optional<int> opt1;
    std::optional<int> opt2 = opt1;
    opt2.value();  // ERROR: copied empty state  // dtt-error {{_has_value}}
}

// Variant 7.2: move from empty optional
void test_move_empty() {
    std::optional<int> opt1;
    std::optional<int> opt2 = std::move(opt1);
    *opt2;  // ERROR: moved empty state  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 8: Conditional without early return
// =============================================================================

// Variant 8.1: conditional set but always access
void test_conditional_set(bool cond) {
    std::optional<int> opt;
    if (cond) {
        opt = 42;
    }
    opt.value();  // ERROR: only has value if cond was true  // dtt-error {{_has_value}}
}

// Variant 8.2: emplace in one branch
void test_emplace_one_branch(bool cond) {
    std::optional<int> opt;
    if (cond) {
        opt.emplace(42);
    }
    *opt;  // ERROR: only has value if cond was true  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 9: Nullopt initialization then access
// =============================================================================

// Variant 9.1: nullopt initialization
void test_nullopt_init() {
    std::optional<int> opt = std::nullopt;
    opt.value();  // KNOWN GAP (not asserted): _has_value == false
}

// Variant 9.2: default + guard false branch
void test_nullopt_default() {
    std::optional<int> opt{};  // default constructs to nullopt
    *opt;  // ERROR  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 10: Function returning optional, unguarded access
// =============================================================================

std::optional<int> maybe_get(bool b) {
    if (b) return 42;
    return std::nullopt;
}

// Variant 10.1: unguarded access to return value
void test_return_unguarded() {
    auto opt = maybe_get(false);
    opt.value();  // KNOWN GAP (not asserted): might be nullopt
}

// Variant 10.2: chained call unguarded
void test_chained_unguarded() {
    maybe_get(true).value();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove has_value at compile time
}

// =============================================================================
// SECTION 11: Comparison doesn't prove has_value in else
// =============================================================================

// Variant 11.1: comparison false branch
void test_comparison_else() {
    std::optional<int> opt;
    if (opt == 42) {
        // opt has value here
    } else {
        opt.value();  // ERROR: opt might not have value here  // dtt-error {{_has_value}}
    }
}

// =============================================================================
// SECTION 12: Nested optional access
// =============================================================================

// Variant 12.1: nested optional, outer not checked
void test_nested_outer() {
    std::optional<std::optional<int>> outer;
    (*outer).value();  // ERROR: outer might be empty  // dtt-error {{_has_value}}
}

// Variant 12.2: nested optional, inner not checked
void test_nested_inner() {
    std::optional<std::optional<int>> outer = std::optional<int>{};
    if (outer) {
        (*outer).value();  // KNOWN GAP (not asserted): inner optional is empty
    }
}

// =============================================================================
// SECTION 13: and_then / transform (C++23) - unguarded lambda
// =============================================================================

// Note: These are C++23, but showing the pattern
// void test_transform_lambda() {
//     std::optional<int> opt;
//     opt.transform([](int x) { return x * 2; });  // Would be OK
//     // But accessing result without guard would be ERROR
// }

int main() {
    return 0;
}
