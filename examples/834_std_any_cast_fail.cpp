// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 834_std_any_cast_fail.cpp
// Tests: std::any_cast on empty any - should FAIL

#include <any>
#include <string>

// =============================================================================
// SECTION 1: any_cast on empty any (value form - throws)
// =============================================================================

// Variant 1.1: any_cast on default-constructed any
void test_cast_empty() {
    std::any a;
    std::any_cast<int>(a);  // ERROR: _has_value = 0  // dtt-error {{_has_value}}
}

// Variant 1.2: any_cast after reset
void test_cast_after_reset() {
    std::any a = 42;
    a.reset();
    std::any_cast<int>(a);  // ERROR: _has_value = 0  // dtt-error {{_has_value}}
}

// =============================================================================
// SECTION 2: any_cast on empty any (pointer form - returns nullptr)
// =============================================================================

// Variant 2.1: any_cast pointer on empty, then dereference
void test_cast_ptr_empty() {
    std::any a;
    int* p = std::any_cast<int>(&a);
    *p;  // ERROR: p is nullptr  // dtt-error {{null pointer dereference}}
}

// Variant 2.2: any_cast pointer after reset
void test_cast_ptr_after_reset() {
    std::any a = 42;
    a.reset();
    *std::any_cast<int>(&a);  // ERROR: returns nullptr  // dtt-error {{_has_value}}
}

int main() { return 0; }
