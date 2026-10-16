// 1742: Clamp idiom (known false positive)
//
// After `if (n < 0) n = 0;`, n >= 0 on both paths: 0 on one, the negated
// guard on the other. DTT merges a value written on one branch to unknown
// and does not combine it with the other branch's guard, so the call is
// "cannot verify". Rename to _pass when the merge keeps per-branch facts.

[[clang::annotate("dtt:requires=n >= 0")]] void take_size(int n);

void clamp(int n) {
    if (n < 0) n = 0;
    take_size(n);
}

int main() { return 0; }
