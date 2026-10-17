// 1760: Value-model gaps (known false negatives, step 4: typed values)
//
// A narrow loop counter that wraps is forgotten, and then heuristic #2
// (docs/HEURISTICS.md: a signed index that can only be negative is accepted)
// accepts the negative index; a container reached through a dereference is
// not tracked. Each access below can be out of bounds; DTT does not report it.
// Rename to _fail (with dtt-error markers) when the value model covers it.

#include <memory>
#include <vector>

int arr200[200];

// Variant 1: a signed char counter wraps from 127 to -128.
void variant1_narrow_counter() {
    for (signed char ch = 0; ch < 200; ++ch) arr200[ch] = 0;
}

// Variant 2: a container reached by dereferencing a pointer or a smart
// pointer is not tracked: (*p)[i], (*sp)[i] and sp->at(i) go unchecked.
int variant2_deref_container(std::vector<int> *p, std::shared_ptr<std::vector<int>> sp) {
    if (!p || !sp) return 0;
    return (*p)[2] + (*sp)[2] + sp->at(2);
}

int main() { return 0; }
