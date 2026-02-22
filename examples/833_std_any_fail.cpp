// 833_std_any_fail.cpp
// Tests: Unsafe std::any usage that DTT SHOULD catch

#include <any>
#include <string>

// =============================================================================
// SECTION 1: any_cast without type guard
// =============================================================================

// Variant 1.1: any_cast on wrong type (throws bad_any_cast)
void test_cast_wrong_type() {
    std::any a = 42;
    std::any_cast<std::string>(a);  // ERROR: a holds int, not string
}

// Variant 1.2: any_cast on parameter any
void test_cast_param(std::any& a) {
    std::any_cast<int>(a);  // ERROR: can't prove a holds int
}

// Variant 1.3: any_cast on empty any
void test_cast_empty() {
    std::any a;
    std::any_cast<int>(a);  // ERROR: a is empty
}

// =============================================================================
// SECTION 2: Access in wrong branch
// =============================================================================

// Variant 2.1: cast in else of type check
void test_wrong_branch() {
    std::any a = 42;
    if (a.type() == typeid(int)) {
        // OK here
    } else {
        std::any_cast<int>(a);  // ERROR: type doesn't match in else
    }
}

// Variant 2.2: cast in else of has_value
void test_wrong_branch_empty() {
    std::any a;
    if (a.has_value()) {
        // OK here (still need type check)
    } else {
        std::any_cast<int>(a);  // ERROR: empty in else branch
    }
}

// =============================================================================
// SECTION 3: any_cast after type change
// =============================================================================

// Variant 3.1: cast wrong type after reassignment
void test_after_reassign() {
    std::any a = 42;
    a = std::string("hello");  // Now holds string
    std::any_cast<int>(a);  // ERROR: a now holds string
}

// Variant 3.2: stale type assumption
void test_stale_type() {
    std::any a = 42;
    bool was_int = (a.type() == typeid(int));
    a = 3.14;  // Changed to double
    if (was_int) {
        std::any_cast<int>(a);  // ERROR: stale check, a changed
    }
}

// =============================================================================
// SECTION 4: any_cast pointer without null check
// =============================================================================

// Variant 4.1: dereference any_cast pointer without checking
void test_cast_ptr_no_check() {
    std::any a = std::string("hello");
    int* p = std::any_cast<int>(&a);
    *p;  // ERROR: p is nullptr (a holds string)
}

// Variant 4.2: any_cast pointer on parameter
void test_cast_ptr_param(std::any& a) {
    *std::any_cast<int>(&a);  // ERROR: can't prove a holds int
}

// =============================================================================
// SECTION 5: any_cast on moved-from any
// =============================================================================

// Variant 5.1: access moved-from any
void test_moved_from() {
    std::any a1 = std::string("hello");
    std::any a2 = std::move(a1);
    std::any_cast<std::string>(a1);  // ERROR: moved-from state
}

// =============================================================================
// SECTION 6: any_cast after reset
// =============================================================================

// Variant 6.1: cast after reset
void test_after_reset() {
    std::any a = 42;
    a.reset();
    std::any_cast<int>(a);  // ERROR: a was reset, now empty
}

// =============================================================================
// SECTION 7: Check has_value but not type
// =============================================================================

// Variant 7.1: has_value doesn't guarantee type
void test_has_value_wrong_type() {
    std::any a = 42;
    if (a.has_value()) {
        std::any_cast<std::string>(a);  // ERROR: has value but wrong type
    }
}

// =============================================================================
// SECTION 8: Multiple anys - wrong one checked
// =============================================================================

// Variant 8.1: check a1, cast a2
void test_wrong_any_checked() {
    std::any a1 = 42;
    std::any a2 = std::string("hello");
    if (a1.type() == typeid(int)) {
        std::any_cast<int>(a2);  // ERROR: checked a1, not a2
    }
}

// =============================================================================
// SECTION 9: any_cast after swap
// =============================================================================

// Variant 9.1: stale type after swap
void test_after_swap() {
    std::any a1 = 42;
    std::any a2 = std::string("hello");
    std::swap(a1, a2);
    std::any_cast<int>(a1);  // ERROR: a1 now holds string
}

// =============================================================================
// SECTION 10: any_cast with wrong reference type
// =============================================================================

// Variant 10.1: const cast on non-const
void test_wrong_cv() {
    const std::any a = 42;
    // This would work: std::any_cast<int>(a);
    // But this forces a copy, not what's typically intended
    (void)a;
}

int main() {
    return 0;
}
