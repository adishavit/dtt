// 1760: Value-model gaps (known false negatives, step 4: typed values)
//
// DTT computes integer values as mathematical integers: a narrow type that
// wraps around is not modeled, and a call does not invalidate globals it may
// write. Each access below can be out of bounds; DTT does not report it.
// Rename to _fail (with dtt-error markers) when the value model covers it.

int arr200[200];
int a100[100];
int a300[300];
int arr10[10];

// Variant 1: a signed char counter wraps from 127 to -128.
void variant1_narrow_counter() {
    for (signed char ch = 0; ch < 200; ++ch) arr200[ch] = 0;
}

// Variant 2: narrow values wrap on ++ and --.
int variant2_narrow_step() {
    signed char c = -128;
    --c;  // 127
    unsigned char u = 255;
    ++u;  // 0
    return a100[c + 129] + a300[u - 1];
}

// Variant 3: a global loop variable a called function writes.
int g_i;
void bump() { g_i = 100; }
int variant3_global_counter() {
    int s = 0;
    for (g_i = 0; g_i < 10; ++g_i) {
        bump();
        s += arr10[g_i];
    }
    return s;
}

int main() { return 0; }
