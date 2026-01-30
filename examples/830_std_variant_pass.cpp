// 830_std_variant_pass.cpp
// Tests: Safe std::variant usage that should compile without DTT errors

#include <variant>
#include <string>

// =============================================================================
// SECTION 1: Guard with holds_alternative before get
// =============================================================================

// Variant 1.1: if (holds_alternative<T>) guard then get<T>
void test_holds_alternative_guard() {
    std::variant<int, std::string> v = 42;
    if (std::holds_alternative<int>(v)) {
        std::get<int>(v);  // OK: guarded
    }
}

// Variant 1.2: holds_alternative with string
void test_holds_alternative_string() {
    std::variant<int, std::string> v = std::string("hello");
    if (std::holds_alternative<std::string>(v)) {
        std::get<std::string>(v);  // OK: guarded
    }
}

// Variant 1.3: early return on wrong type
void test_early_return() {
    std::variant<int, double> v = 3.14;
    if (!std::holds_alternative<double>(v)) return;
    std::get<double>(v);  // OK: guarded
}

// =============================================================================
// SECTION 2: Guard with index() before get<I>
// =============================================================================

// Variant 2.1: if (index() == I) guard
void test_index_guard() {
    std::variant<int, std::string> v = 42;
    if (v.index() == 0) {
        std::get<0>(v);  // OK: index 0 is int
    }
}

// Variant 2.2: switch on index
// NOTE: Switch case proof extraction for v.index() is not yet implemented.
// See 832g_variant_switch_silent.cpp for gap documentation.
// void test_switch_index() {
//     std::variant<int, double, std::string> v = 3.14;
//     switch (v.index()) {
//         case 0: std::get<int>(v); break;
//         case 1: std::get<double>(v); break;
//         case 2: std::get<std::string>(v); break;
//     }
// }

// =============================================================================
// SECTION 3: Use get_if (returns nullptr on wrong type)
// =============================================================================

// Variant 3.1: get_if returns pointer
void test_get_if() {
    std::variant<int, std::string> v = 42;
    if (int* p = std::get_if<int>(&v)) {
        *p;  // OK: get_if succeeded
    }
}

// Variant 3.2: get_if with string
void test_get_if_string() {
    std::variant<int, std::string> v = std::string("hello");
    if (auto* p = std::get_if<std::string>(&v)) {
        p->size();  // OK: get_if succeeded
    }
}

// Variant 3.3: get_if by index
void test_get_if_index() {
    std::variant<int, double> v = 3.14;
    if (auto* p = std::get_if<1>(&v)) {
        *p;  // OK: index 1 is double
    }
}

// =============================================================================
// SECTION 4: std::visit (always safe)
// =============================================================================

// Variant 4.1: visit with lambda
void test_visit() {
    std::variant<int, std::string> v = 42;
    std::visit([](auto&& arg) {
        (void)arg;  // OK: visit handles all types
    }, v);
}

// Variant 4.2: visit with overloaded
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void test_visit_overloaded() {
    std::variant<int, double, std::string> v = std::string("hello");
    std::visit(overloaded{
        [](int i) { (void)i; },
        [](double d) { (void)d; },
        [](const std::string& s) { (void)s.size(); }
    }, v);  // OK: all types handled
}

// =============================================================================
// SECTION 5: Access after assignment (known type)
// =============================================================================

// Variant 5.1: get after direct assignment
void test_after_assign() {
    std::variant<int, std::string> v;
    v = 42;
    std::get<int>(v);  // OK: just assigned int
}

// Variant 5.2: get after emplace
void test_after_emplace() {
    std::variant<int, std::string> v;
    v.emplace<std::string>("hello");
    std::get<std::string>(v);  // OK: just emplaced string
}

// Variant 5.3: get after emplace by index
void test_after_emplace_index() {
    std::variant<int, double> v;
    v.emplace<1>(3.14);
    std::get<1>(v);  // OK: just emplaced at index 1
}

// =============================================================================
// SECTION 6: Copy preserves type
// =============================================================================

// Variant 6.1: copy variant
void test_copy() {
    std::variant<int, std::string> v1 = 42;
    std::variant<int, std::string> v2 = v1;
    if (std::holds_alternative<int>(v2)) {
        std::get<int>(v2);  // OK: copied from int
    }
}

// =============================================================================
// SECTION 7: valueless_by_exception check
// =============================================================================

// Variant 7.1: check not valueless before access
void test_valueless_check() {
    std::variant<int, std::string> v = 42;
    if (!v.valueless_by_exception()) {
        // Safe to access - not in invalid state
        if (std::holds_alternative<int>(v)) {
            std::get<int>(v);  // OK: double guarded
        }
    }
}

// =============================================================================
// SECTION 8: Default construction (first type)
// =============================================================================

// Variant 8.1: default constructed has index 0
void test_default_construct() {
    std::variant<int, std::string> v;  // default constructs int
    if (v.index() == 0) {
        std::get<0>(v);  // OK: default is first type
    }
}

// Variant 8.2: monostate as first type
void test_monostate() {
    std::variant<std::monostate, int, std::string> v;
    if (std::holds_alternative<std::monostate>(v)) {
        // OK: monostate is default
    }
}

// =============================================================================
// SECTION 9: Comparison implies same type
// =============================================================================

// Variant 9.1: equality check
void test_equality() {
    std::variant<int, std::string> v1 = 42;
    std::variant<int, std::string> v2 = 42;
    if (v1 == v2) {
        // Both have same index and value
        if (std::holds_alternative<int>(v1)) {
            std::get<int>(v1);  // OK
        }
    }
}

int main() {
    return 0;
}
