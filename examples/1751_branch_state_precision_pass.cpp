// 1751: Branch state - facts that hold on every path stay known
//
// Each function below is safe and DTT can prove it. Runs in enforce mode, so
// "cannot verify" fails the test.
#include <cstdio>

int a[4];

// Variant 1: a file closed on both branches is closed after the `if`.
void variant1_close_on_both_paths(int n) {
    FILE *f = fopen("a", "r");
    if (!f) return;
    if (n < 0 || n > 9) {
        fclose(f);
    } else {
        fclose(f);
    }
}

// Variant 2: the else branch still has the file open.
void variant2_else_branch_state(int n) {
    FILE *f = fopen("a", "r");
    if (!f) return;
    if (n > 0) {
        fclose(f);
    } else {
        fgetc(f);
        fclose(f);
    }
}

// Variant 3: proofs from earlier guards survive a later guard on the same variable.
int variant3_guard_keeps_earlier_proof(int n) {
    if (n < 0) return 0;
    if (n > 3) return 0;
    if (n <= 3) {
        a[0] = 1;
    }
    return a[n];
}

[[clang::annotate("dtt:requires=arg0 >= 0")]] void need_nonneg(int n);

// Variant 4: i > 3 on one path and i >= 0 on the other give i >= 0.
void variant4_weaker_fact_on_both_paths(int i, int c) {
    if (c) {
        if (i <= 3) return;
    } else {
        if (i < 0) return;
    }
    need_nonneg(i);
}

int main() { return 0; }
