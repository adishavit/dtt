// 1760: Value-model gaps (known false negatives, step 4: typed values)
//
// A narrow loop counter that wraps is forgotten, and then heuristic #2
// (docs/HEURISTICS.md: a signed index that can only be negative is accepted)
// accepts the negative index; a call does not invalidate globals it may
// write; a container reached through a dereference is not tracked. Each
// access below can be out of bounds; DTT does not report it.
// Rename to _fail (with dtt-error markers) when the value model covers it.

#include <memory>
#include <vector>

int arr200[200];
int arr10[10];

// Variant 1: a signed char counter wraps from 127 to -128.
void variant1_narrow_counter() {
    for (signed char ch = 0; ch < 200; ++ch) arr200[ch] = 0;
}

// Variant 2: a global loop variable a called function writes.
int g_i;
void bump() { g_i = 100; }
int variant2_global_counter() {
    int s = 0;
    for (g_i = 0; g_i < 10; ++g_i) {
        bump();
        s += arr10[g_i];
    }
    return s;
}

// Variant 3: a container reached by dereferencing a pointer or a smart
// pointer is not tracked: (*p)[i], (*sp)[i] and sp->at(i) go unchecked.
int variant3_deref_container(std::vector<int> *p, std::shared_ptr<std::vector<int>> sp) {
    if (!p || !sp) return 0;
    return (*p)[2] + (*sp)[2] + sp->at(2);
}

int main() { return 0; }
