// 840_or_guard_lifetime_pass.cpp
// Tests: OR-guard else branch should not inherit then-branch frees

#include <cstdlib>

// Variant 1: OR guard with free in then, use in else
void test_or_guard_then_free_else_use(bool flag) {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;

    if (p == nullptr || flag) {
        free(p);
    } else {
        *p = 1;  // OK: else branch should not see then-branch free
        free(p);
    }
}

// Variant 2: OR guard with guard on right side
void test_or_guard_right_free_else_use(bool flag) {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;

    if (flag || p == nullptr) {
        free(p);
    } else {
        *p = 2;  // OK: else branch should not see then-branch free
        free(p);
    }
}

int main() { return 0; }
