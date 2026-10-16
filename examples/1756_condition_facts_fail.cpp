// 1756: Condition facts - a condition proves nothing on the path it does not take
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

int check();

// Variant 1: !(p && check()) does not mean p is null.
int variant1_partial_and(int *p) {
    if (p && check()) return 0;
    return *p;  // dtt-error {{nullability unknown}}
}

// Variant 2: the false arm of ?: does not know the pointer is non-null.
int variant2_ternary_false_arm(int *p) {
    return p ? 0 : *p;  // dtt-error {{known to be null}}
}

// Variant 3: the right operand of || runs when the left one is false.
bool variant3_or_when_null(int *p) {
    return p || *p;  // dtt-error {{known to be null}}
}

// Variant 4: a dereference inside an if condition is checked.
int variant4_deref_in_condition(int n) {
    int *q = nullptr;
    if (n > 0 && *q) return 1;  // dtt-error {{known to be null}}
    return 0;
}

int a[4];
struct Buf { int _size; };

// Variant 5: a later operand that nulls p is not overridden by the guard.
bool variant5_reassigned_in_and(int *p) {
    return p && (p = nullptr, true) && *p;  // dtt-error {{known to be null}}
}

// Variant 6: bounds checked before x is reassigned do not bound the new x.
int variant6_reassigned_bound(int x, int y) {
    return x >= 0 && x < 4 && (x = y, 1) && a[x];  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 7: the then branch sees the assignment in the condition.
int variant7_reassigned_in_if(int *p) {
    if (p && (p = nullptr, true)) return *p;  // dtt-error {{known to be null}}
    return 0;
}

// Variant 8: a write after the check replaces what the check proved.
int variant8_reset_after_check(std::optional<int> o) {
    return o.has_value() && (o.reset(), true) && *o;  // dtt-error {{_has_value}}
}

// Variant 9: reassigning the index drops its bound.
int variant9_index_reassigned(const std::vector<int> &v, size_t i, size_t j) {
    return i < v.size() && (i = j, true) && v[i];  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 10: (unsigned)n > 3 holds for negative n too.
int variant10_unsigned_cast(int n) {
    return (unsigned)n > 3u ? a[7 - n] : 0;  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 11: a narrowing cast says nothing about the original value.
int variant11_narrowing_cast(int n) {
    return ((signed char)n > 3 && n < 8) ? a[7 - n] : 0;  // dtt-error {{cannot verify|out of bounds}}
}

// Variant 12: __builtin_expect still reads its argument.
int variant12_expect_reads_argument() {
    int x;
    if (__builtin_expect(x, 0)) return 1;  // dtt-error {{uninitialized}}
    return 0;
}

// Variant 13: n > 3u converts n to unsigned; a negative n passes the test.
int variant13_implicit_unsigned(int n) {
    return n > 3u ? a[7 - n] : 0;  // dtt-error {{cannot verify|out of bounds}}
}

int main() { return 0; }
