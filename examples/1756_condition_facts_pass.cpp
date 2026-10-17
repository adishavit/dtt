// 1756: Condition facts - what a condition says holds on each of its paths
//
// Each function below is safe. DTT used conditions only as the guard of an
// if statement; the arms of ?: and the right operand of && and || did not
// know what the left side had established.
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <string>
#include <cstddef>
#include <vector>

int a[4];
int g;

// Variant 1: ?: arms know the condition.
int variant1_ternary(int *p) {
    return p ? *p : 0;
}

// Variant 2: the right operand of && knows the left one is true.
bool variant2_and(int *p) {
    return p && *p > 0;
}

// Variant 3: the right operand of || knows the left one is false.
bool variant3_or(int *p) {
    return !p || *p > 0;
}

// Variant 4: an index bounded by the condition of ?:.
int variant4_ternary_bound(int k) {
    return (k >= 0 && k < 4) ? a[k] : 0;
}

// Variant 5: an assert's condition holds after it.
int variant5_assert(int *p) {
    assert(p);
    return *p;
}

// Variant 6: an if condition's own && is checked in order.
int variant6_if_and(int *p) {
    if (p && *p > 0) return 1;
    return 0;
}

// Variant 7: an assignment inside a condition happens (zlib inflate).
int variant7_assignment_in_condition(int c, int *q) {
    int len;
    if (q && (len = *q - c) < 4) return len;
    return 0;
}

// Variant 8: a bool contract in the right operand of &&.
int variant8_bool_contract(const std::vector<int> &v, int c) {
    if (c && !v.empty()) return v[0];
    return 0;
}

// Variant 9: __builtin_expect keeps the condition's meaning.
int variant9_builtin_expect(int *p) {
    if (__builtin_expect(!p, 0)) return 0;
    return *p;
}

// Variant 10: an assignment tested in the condition.
int variant10_assignment_tested(int *q) {
    int *r = nullptr;
    if (q && (r = q)) return *r;
    return 0;
}

// Variant 11: a value assigned in the condition reaches the branch.
int variant11_assigned_value(int *q) {
    int len = 9;
    if (q && (len = 2)) return a[len];
    return 0;
}

// Variant 12: a pointer assigned in the condition is initialized.
int variant12_assign_then_compare() {
    FILE *f;
    if ((f = fopen("a", "r")) == NULL) return 1;
    fclose(f);
    return 0;
}

bool check(const std::string &s);

// Variant 13: a temporary in the condition does not hide its guards.
int variant13_temporary_in_condition(int *p) {
    if (p && check(std::string("x"))) return *p;
    return 0;
}

// Variant 14: (unsigned)n < 4 bounds n on its true side.
int variant14_unsigned_bound(int n) {
    return (unsigned)n < 4u && n >= 0 ? a[n] : 0;
}

// Variant 15: calls that keep or grow the size keep the size guard.
int variant15_size_guard_survives(std::vector<int> &v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    v.push_back(1);
    return v[0];
}

int produce();

// Variant 16: the right operand of && reads x after the left one wrote it.
bool variant16_assigned_then_read() {
    int x;
    return (x = produce()) && x;
}

int main() { return 0; }
