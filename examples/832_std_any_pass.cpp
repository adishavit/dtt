// 832_std_any_pass.cpp
// Tests: Safe std::any usage that should compile without DTT errors

#include <any>
#include <string>

// =============================================================================
// SECTION 1: Guard with has_value before any_cast
// =============================================================================

// Variant 1.1: if (has_value()) guard
void test_has_value_guard() {
    std::any a = 42;
    if (a.has_value()) {
        // Safe to attempt cast (though type must still match)
    }
}

// =============================================================================
// SECTION 2: Guard with type() before any_cast
// =============================================================================

// Variant 2.1: if (type() == typeid(T)) guard then any_cast<T>
void test_type_guard() {
    std::any a = 42;
    if (a.type() == typeid(int)) {
        std::any_cast<int>(a);  // OK: type matches
    }
}

// Variant 2.2: type guard with string
void test_type_guard_string() {
    std::any a = std::string("hello");
    if (a.type() == typeid(std::string)) {
        std::any_cast<std::string>(a);  // OK: type matches
    }
}

// Variant 2.3: early return on wrong type
void test_early_return() {
    std::any a = 3.14;
    if (a.type() != typeid(double)) return;
    std::any_cast<double>(a);  // OK: guarded
}

// =============================================================================
// SECTION 3: Use any_cast pointer form (returns nullptr on wrong type)
// =============================================================================

// Variant 3.1: any_cast to pointer then check
void test_any_cast_ptr() {
    std::any a = 42;
    if (int* p = std::any_cast<int>(&a)) {
        *p;  // OK: any_cast succeeded
    }
}

// Variant 3.2: any_cast pointer with string
void test_any_cast_ptr_string() {
    std::any a = std::string("hello");
    if (auto* p = std::any_cast<std::string>(&a)) {
        p->size();  // OK: any_cast succeeded
    }
}

// Variant 3.3: any_cast pointer null check
void test_any_cast_ptr_null_check() {
    std::any a = 42;
    auto* p = std::any_cast<double>(&a);
    if (p != nullptr) {
        *p;  // OK: explicit null check
    }
}

// =============================================================================
// SECTION 4: Access after assignment (known type)
// =============================================================================

// Variant 4.1: any_cast after direct assignment
void test_after_assign() {
    std::any a;
    a = 42;
    if (a.type() == typeid(int)) {
        std::any_cast<int>(a);  // OK: just assigned int
    }
}

// Variant 4.2: any_cast after emplace
void test_after_emplace() {
    std::any a;
    a.emplace<std::string>("hello");
    if (a.type() == typeid(std::string)) {
        std::any_cast<std::string>(a);  // OK: just emplaced
    }
}

// Variant 4.3: any_cast after make_any
void test_make_any() {
    auto a = std::make_any<int>(42);
    if (a.type() == typeid(int)) {
        std::any_cast<int>(a);  // OK: make_any with int
    }
}

// =============================================================================
// SECTION 5: Copy preserves type
// =============================================================================

// Variant 5.1: copy any
void test_copy() {
    std::any a1 = 42;
    std::any a2 = a1;
    if (a2.type() == typeid(int)) {
        std::any_cast<int>(a2);  // OK: copied from int any
    }
}

// =============================================================================
// SECTION 6: Check has_value and type together
// =============================================================================

// Variant 6.1: both guards
void test_both_guards() {
    std::any a = 42;
    if (a.has_value() && a.type() == typeid(int)) {
        std::any_cast<int>(a);  // OK: double guarded
    }
}

// =============================================================================
// SECTION 7: try-catch around any_cast
// =============================================================================

// Variant 7.1: catch bad_any_cast
void test_try_catch() {
    std::any a = 42;
    try {
        std::any_cast<int>(a);  // OK: wrapped in try-catch
    } catch (const std::bad_any_cast&) {
        // Handle error
    }
}

// =============================================================================
// SECTION 8: Default constructed any
// =============================================================================

// Variant 8.1: check has_value on empty any
void test_empty_check() {
    std::any a;
    if (!a.has_value()) {
        // OK: correctly identified empty
    }
}

// Variant 8.2: don't access empty any
void test_empty_no_access() {
    std::any a;
    if (a.has_value()) {
        // Only access if has value
        if (a.type() == typeid(int)) {
            std::any_cast<int>(a);
        }
    }
}

// =============================================================================
// SECTION 9: Reset and recheck
// =============================================================================

// Variant 9.1: reset then check
void test_reset_check() {
    std::any a = 42;
    a.reset();
    if (!a.has_value()) {
        // OK: correctly empty after reset
    }
}

// Variant 9.2: reassign after reset
void test_reset_reassign() {
    std::any a = 42;
    a.reset();
    a = std::string("hello");
    if (a.type() == typeid(std::string)) {
        std::any_cast<std::string>(a);  // OK: new value
    }
}

int main() {
    return 0;
}
