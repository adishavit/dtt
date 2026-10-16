// 1742: A value written on only one branch is unknown after the branch
//
// Writes whose effect is the same on every path stay known. Runs in enforce
// mode, so a call DTT cannot verify fails the test.

[[clang::annotate("dtt:requires=n >= 0")]] void take_size(int n);

// Variant 1: the same value on both branches.
void variant1_same_value(int n, bool c) {
    if (c) n = 5;
    else n = 5;
    take_size(n);
}

// Variant 2: overwritten after the branch.
void variant2_overwritten(int n, bool c) {
    if (c) n = -5;
    n = 3;
    take_size(n);
}

// Variant 3: the writing branch returns, so its value never reaches the call.
void variant3_writing_branch_returns(int n, bool c) {
    if (n < 0) return;
    if (c) {
        n = -1;
        return;
    }
    take_size(n);
}

int main() { return 0; }
