// 985_main_argcount_pass.cpp
// Tests: main's first int parameter works regardless of name - should PASS
// The constraint is on position, not name

#include <vector>

// Different parameter name - should still work
int main(int arg_count, char** args) {
    std::vector<int> v;
    v.resize(arg_count);
    v[0] = 42;  // OK: first param of main >= 1 guaranteed
    return 0;
}
