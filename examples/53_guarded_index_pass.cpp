// Example 53: Guarded runtime index — should COMPILE
// DTT tracks if (i < v.size()) guards for runtime indices

#include "../include/dtt_vector.h"

int getInput();

void test_simple_guard() {
    dtt::vector<int> v;
    v.resize(10);

    int i = getInput();  // Unknown runtime value

    if (i < v.size()) {
        v[i] = 42;  // OK: guard proves i < _size
    }
}

void test_compound_guard() {
    dtt::vector<int> v;
    v.resize(10);

    int idx = getInput();

    // Compound guard: i >= 0 && i < v.size()
    if (idx >= 0 && idx < v.size()) {
        v[idx] = 99;  // OK: upper bound proven
    }
}

void test_guard_with_member() {
    struct Data { int len; int* data; };
    Data d;
    d.len = 5;

    int i = getInput();

    if (i < d.len) {
        // Would need raw array support, but shows pattern
    }
}

int main() {
    test_simple_guard();
    test_compound_guard();
    return 0;
}
