// 1750: Contradictory loop facts (known false positives)
//
// Contradictory facts prove nothing, so these safe accesses are "cannot
// verify" (an error in enforce mode). Rename to _pass when DTT derives
// consistent facts for them.

int a[10];

// Variant 1: a count-down loop gets bound facts i >= 9 and i <= 0.
void variant1_count_down() {
    for (int i = 9; i >= 0; i--)
        a[i] = i;
}

// Variant 2: a loop body that never runs (0 <= i < 0).
void variant2_empty_loop() {
    int n = 0;
    for (int i = 0; i < n; i++)
        a[i] = i;
}

int main() { return 0; }
