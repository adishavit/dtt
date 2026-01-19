// 804_std_optional_pass.cpp
// Tests: Safe std::optional usage patterns that should compile without DTT errors

#include <optional>

// =============================================================================
// SECTION 1: has_value() guards
// =============================================================================

// Variant 1.1: has_value() guard before value()
void test_has_value_guard() {
    std::optional<int> opt = 42;
    if (opt.has_value()) {
        opt.value();  // OK: has_value() proves _has_value
    }
}

// Variant 1.2: has_value() guard before operator*
void test_has_value_deref() {
    std::optional<int> opt = 42;
    if (opt.has_value()) {
        *opt;  // OK
    }
}

// Variant 1.3: has_value() guard before operator->
void test_has_value_arrow() {
    struct S { int x; };
    std::optional<S> opt = S{42};
    if (opt.has_value()) {
        opt->x;  // OK
    }
}

// =============================================================================
// SECTION 2: bool conversion guards
// =============================================================================

// Variant 2.1: if (opt) guard
void test_bool_guard() {
    std::optional<int> opt = 42;
    if (opt) {
        opt.value();  // OK: bool conversion proves _has_value
    }
}

// Variant 2.2: if (!opt) else access
void test_bool_else() {
    std::optional<int> opt = 42;
    if (!opt) {
        return;
    }
    opt.value();  // OK: !opt returned, so opt has value here
}

// Variant 2.3: bool in while
void test_bool_while() {
    std::optional<int> opt = 42;
    while (opt) {
        *opt;  // OK
        opt.reset();
    }
}

// =============================================================================
// SECTION 3: Direct initialization
// =============================================================================

// Variant 3.1: Initialize with value
void test_init_value() {
    std::optional<int> opt = 42;  // _has_value = true
    opt.value();                  // OK
}

// Variant 3.2: in_place constructor
void test_in_place() {
    std::optional<int> opt(std::in_place, 42);  // _has_value = true
    *opt;  // OK
}

// Variant 3.3: make_optional
void test_make_optional() {
    auto opt = std::make_optional(42);  // _has_value = true
    opt.value();  // OK
}

// =============================================================================
// SECTION 4: emplace() tracking
// =============================================================================

// Variant 4.1: emplace() sets has_value
void test_emplace() {
    std::optional<int> opt;
    opt.emplace(42);  // _has_value = true
    opt.value();      // OK
}

// Variant 4.2: emplace() after reset
void test_emplace_after_reset() {
    std::optional<int> opt = 42;
    opt.reset();
    opt.emplace(100);  // _has_value = true
    *opt;              // OK
}

// =============================================================================
// SECTION 5: Assignment tracking
// =============================================================================

// Variant 5.1: assign value
void test_assign_value() {
    std::optional<int> opt;
    opt = 42;      // _has_value = true
    opt.value();   // OK
}

// Variant 5.2: assign std::nullopt clears, then assign value
void test_assign_nullopt_then_value() {
    std::optional<int> opt = 42;
    opt = std::nullopt;  // _has_value = false
    opt = 100;           // _has_value = true
    *opt;                // OK
}

// =============================================================================
// SECTION 6: reset() and rebuild
// =============================================================================

// Variant 6.1: reset() then emplace
void test_reset_emplace() {
    std::optional<int> opt = 42;
    opt.reset();      // _has_value = false
    opt.emplace(0);   // _has_value = true
    opt.value();      // OK
}

// Variant 6.2: reset() then guard
void test_reset_guard() {
    std::optional<int> opt = 42;
    opt.reset();
    if (opt.has_value()) {
        opt.value();  // OK: guarded (won't execute)
    }
}

// =============================================================================
// SECTION 7: value_or() is always safe
// =============================================================================

// Variant 7.1: value_or() on empty optional
void test_value_or_empty() {
    std::optional<int> opt;
    opt.value_or(0);  // OK: value_or is safe even if empty
}

// Variant 7.2: value_or() on set optional
void test_value_or_set() {
    std::optional<int> opt = 42;
    opt.value_or(-1);  // OK: returns 42
}

// =============================================================================
// SECTION 8: Copy/move preserves has_value
// =============================================================================

// Variant 8.1: copy constructor
void test_copy() {
    std::optional<int> opt1 = 42;
    std::optional<int> opt2 = opt1;
    if (opt2.has_value()) {
        opt2.value();  // OK
    }
}

// Variant 8.2: copy assignment
void test_copy_assign() {
    std::optional<int> opt1 = 42;
    std::optional<int> opt2;
    opt2 = opt1;
    if (opt2) {
        *opt2;  // OK
    }
}

// Variant 8.3: move constructor
void test_move() {
    std::optional<int> opt1 = 42;
    std::optional<int> opt2 = std::move(opt1);
    if (opt2) {
        opt2.value();  // OK
    }
}

// =============================================================================
// SECTION 9: swap() with known optionals
// =============================================================================

// Variant 9.1: swap known optionals
void test_swap() {
    std::optional<int> opt1 = 42;
    std::optional<int> opt2;
    opt1.swap(opt2);
    // opt2 now has value, opt1 doesn't
    if (opt2) {
        *opt2;  // OK
    }
}

// =============================================================================
// SECTION 10: Comparisons with guards
// =============================================================================

// Variant 10.1: simple has_value check
void test_comparison() {
    std::optional<int> opt = 42;
    if (opt.has_value()) {  // Simple has_value check
        opt.value();  // OK: has_value() guard
    }
}

// Variant 10.2: checking has_value before comparing
void test_comparison_guard() {
    std::optional<int> opt = 42;
    if (opt.has_value()) {
        if (*opt == 42) {
            *opt;  // OK: has_value() guard
        }
    }
}

// =============================================================================
// SECTION 11: Nested optional
// =============================================================================

// Variant 11.1: nested optional with guards
void test_nested() {
    std::optional<std::optional<int>> outer = std::make_optional(std::make_optional(42));
    if (outer) {
        if (*outer) {
            (*outer).value();  // OK: both guards passed
        }
    }
}

// =============================================================================
// SECTION 12: Optional of pointer (separate from nullability)
// =============================================================================

// Variant 12.1: optional<int*> with guards
void test_optional_ptr() {
    int x = 42;
    std::optional<int*> opt = &x;
    if (opt.has_value()) {
        int* p = opt.value();  // OK: optional has value
        // Note: p itself may be null, separate concern
    }
}

// =============================================================================
// SECTION 13: Return value checking
// =============================================================================

// Variant 13.1: function returning optional
std::optional<int> get_optional(bool has) {
    if (has) return 42;
    return std::nullopt;
}

void test_return_guard() {
    auto opt = get_optional(true);
    if (opt) {
        opt.value();  // OK: guarded
    }
}

// =============================================================================
// SECTION 14: Logical operators
// =============================================================================

// Variant 14.1: && with has_value
void test_and_guard(bool cond) {
    std::optional<int> opt = 42;
    if (opt.has_value() && cond) {
        *opt;  // OK: has_value is true
    }
}

// Variant 14.2: || early return
void test_or_early_return() {
    std::optional<int> opt = 42;
    if (!opt.has_value()) {
        return;
    }
    opt.value();  // OK: !has_value returned
}

int main() {
    return 0;
}
