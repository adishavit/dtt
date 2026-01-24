// 992_size_guard_constant_index_pass.cpp
// Tests: Size guard should protect constant index access

#include <vector>

// Variant 1: size() > N guard should allow index < N access
void test_size_gt_guard_constant_index() {
    std::vector<int> v;
    if (v.size() > 5) {
        v[0];   // OK: size > 5 means size >= 6, so index 0 is safe
        v[5];   // OK: size > 5 means size >= 6, so index 5 is safe
    }
}

// Variant 2: size() >= N guard should allow index < N access
void test_size_ge_guard_constant_index() {
    std::vector<int> v;
    if (v.size() >= 3) {
        v[0];   // OK: size >= 3 means index 0 is safe
        v[2];   // OK: size >= 3 means index 2 is safe
    }
}

// Variant 3: size() != 0 guard should allow index 0 access
void test_size_ne_zero_guard() {
    std::vector<int> v;
    if (v.size() != 0) {
        v[0];   // OK: size != 0 means size >= 1, so index 0 is safe
    }
}

// Variant 4: Nested guard conditions
void test_nested_guards() {
    std::vector<int> v;
    if (v.size() > 0) {
        if (v.size() > 2) {
            v[2];   // OK: size > 2 means size >= 3
        }
        v[0];   // OK: size > 0 means size >= 1
    }
}

// Variant 5: Guard in else-branch negation
void test_else_branch_guard() {
    std::vector<int> v;
    if (v.size() <= 0) {
        // size is 0 here, no access allowed
    } else {
        v[0];   // OK: size > 0 in else branch
    }
}

int main() { return 0; }
