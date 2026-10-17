// 1750: Contradictory loop facts (known false positive)
//
// Contradictory facts prove nothing, so this safe access is "cannot
// verify" (an error in enforce mode). Rename to _pass when DTT treats a
// condition whose facts contradict each other as unreachable.
// (The count-down loop that was variant 1 is proven now:
// 1758_loop_state_pass.cpp variant 1.)

int a[10];

// Variant 2: a loop body that never runs (0 <= i < 0).
void variant2_empty_loop() {
    int n = 0;
    for (int i = 0; i < n; i++)
        a[i] = i;
}

int main() { return 0; }
