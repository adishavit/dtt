// 831_std_variant_xfail.cpp
// Tests: Unsafe std::variant usage that DTT SHOULD catch

#include <variant>
#include <string>

// =============================================================================
// SECTION 1: get<T> without holds_alternative guard
// =============================================================================

// Variant 1.1: get<T> on wrong type (throws bad_variant_access)
void test_get_wrong_type() {
    std::variant<int, std::string> v = 42;
    std::get<std::string>(v);  // ERROR: v holds int, not string
}

// Variant 1.2: get<I> with wrong index
void test_get_wrong_index() {
    std::variant<int, double> v = 42;
    std::get<1>(v);  // ERROR: v.index() is 0, not 1
}

// Variant 1.3: get on parameter variant
void test_get_param(std::variant<int, std::string>& v) {
    std::get<int>(v);  // ERROR: can't prove v holds int
}

// =============================================================================
// SECTION 2: Access in wrong branch
// =============================================================================

// Variant 2.1: get in else of holds_alternative
void test_wrong_branch() {
    std::variant<int, std::string> v = 42;
    if (std::holds_alternative<int>(v)) {
        // OK here for int
    } else {
        std::get<int>(v);  // ERROR: v doesn't hold int in else branch
    }
}

// Variant 2.2: get in wrong switch case
void test_wrong_case() {
    std::variant<int, double, std::string> v = 42;  // index 0
    switch (v.index()) {
        case 0:
            std::get<1>(v);  // ERROR: index is 0, not 1
            break;
        default:
            break;
    }
}

// =============================================================================
// SECTION 3: get after reassignment
// =============================================================================

// Variant 3.1: get wrong type after reassignment
void test_after_reassign() {
    std::variant<int, std::string> v = 42;
    v = std::string("hello");  // Now holds string
    std::get<int>(v);  // ERROR: v now holds string
}

// Variant 3.2: stale type assumption
void test_stale_type() {
    std::variant<int, double> v = 42;
    bool was_int = std::holds_alternative<int>(v);
    v = 3.14;  // Changed to double
    if (was_int) {
        std::get<int>(v);  // ERROR: stale check, v changed
    }
}

// =============================================================================
// SECTION 4: get_if result used without null check
// =============================================================================

// Variant 4.1: dereference get_if without checking
void test_get_if_no_check() {
    std::variant<int, std::string> v = std::string("hello");
    int* p = std::get_if<int>(&v);
    *p;  // ERROR: p is nullptr (v holds string)
}

// Variant 4.2: get_if on parameter without check
void test_get_if_param(std::variant<int, std::string>& v) {
    *std::get_if<int>(&v);  // ERROR: can't prove v holds int
}

// =============================================================================
// SECTION 5: Access on valueless variant
// =============================================================================

// Variant 5.1: get on potentially valueless variant
// (This can happen if a throwing constructor fails during assignment)
struct Thrower {
    Thrower() = default;
    Thrower(const Thrower&) { throw 42; }
};

void test_valueless_access() {
    std::variant<int, Thrower> v = 42;
    try {
        Thrower t;
        v = t;  // May throw, leaving v valueless
    } catch (...) {}
    // v might be valueless_by_exception
    std::get<int>(v);  // ERROR: v might be valueless
}

// =============================================================================
// SECTION 6: get on moved-from variant
// =============================================================================

// Variant 6.1: access moved-from variant
void test_moved_from() {
    std::variant<int, std::string> v1 = std::string("hello");
    std::variant<int, std::string> v2 = std::move(v1);
    std::get<std::string>(v1);  // ERROR: moved-from state
}

// =============================================================================
// SECTION 7: Type index mismatch
// =============================================================================

// Variant 7.1: check one index, access another
void test_index_mismatch() {
    std::variant<int, double, std::string> v = 3.14;  // index 1
    if (v.index() == 1) {
        std::get<0>(v);  // ERROR: index is 1, not 0
    }
}

// Variant 7.2: check type, access by wrong index
void test_type_index_mismatch() {
    std::variant<int, double> v = 3.14;
    if (std::holds_alternative<double>(v)) {
        std::get<0>(v);  // ERROR: double is at index 1, not 0
    }
}

// =============================================================================
// SECTION 8: Multiple variants - wrong one checked
// =============================================================================

// Variant 8.1: check v1, access v2
void test_wrong_variant_checked() {
    std::variant<int, std::string> v1 = 42;
    std::variant<int, std::string> v2 = std::string("hello");
    if (std::holds_alternative<int>(v1)) {
        std::get<int>(v2);  // ERROR: checked v1, not v2
    }
}

// =============================================================================
// SECTION 9: get on empty monostate
// =============================================================================

// Variant 9.1: get non-monostate on monostate variant
void test_monostate_get() {
    std::variant<std::monostate, int> v;  // default is monostate
    std::get<int>(v);  // ERROR: v holds monostate
}

// =============================================================================
// SECTION 10: Access after swap
// =============================================================================

// Variant 10.1: stale type after swap
void test_after_swap() {
    std::variant<int, std::string> v1 = 42;
    std::variant<int, std::string> v2 = std::string("hello");
    std::swap(v1, v2);
    std::get<int>(v1);  // ERROR: v1 now holds string
}

int main() {
    return 0;
}
