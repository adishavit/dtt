// 1742: A value written on only one branch is unknown after the branch
//
// After `if (c) n = 5;`, n is 5 or its earlier value, so DTT must not treat
// it as 5. Each call below can receive a negative n.
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take_size(int n);

// Variant 1: write in the then-branch, no else.
void variant1_then_only(int n, bool c) {
    if (c) n = 5;
    take_size(n);  // dtt-error {{cannot verify}}
}

// Variant 2: a violating write in the then-branch is not a proof that the
// call always violates.
void variant2_then_only_violating(int n, bool c) {
    if (c) n = -5;
    take_size(n);  // dtt-error {{cannot verify}}
}

// Variant 3: write in the else-branch only.
void variant3_else_only(int n, bool c) {
    if (c) {
        take_size(0);
    } else {
        n = 5;
    }
    take_size(n);  // dtt-error {{cannot verify}}
}

// Variant 4: a container emptied on one branch is not known to be empty.
int variant4_container(std::vector<int> &v, bool c) {
    if (c) v.clear();
    return v[0];  // dtt-error {{cannot verify|unknown}}
}

int main() { return 0; }
