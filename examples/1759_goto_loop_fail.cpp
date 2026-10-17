// 1759: A label a later goto jumps back to is a loop head
//
// DTT continued from the current state at such a label, so its body was
// analyzed as if it ran once: the state the backward goto carries never
// reached the label.
#include <cstdlib>

bool c();
int a[4];

// Variant 1: a counter grown by each pass back through the label.
void variant1_counter() {
    int i = 0;
again:
    a[i] = 1;  // dtt-error {{cannot verify|out of bounds}}
    i++;
    if (c()) goto again;
}

// Variant 2: a pointer freed before jumping back.
void variant2_freed_before_goto() {
    int *p = static_cast<int *>(malloc(sizeof(int)));
    if (!p) return;
again:
    *p = 1;  // dtt-error {{freed on some paths|use-after-free}}
    if (c()) {
        free(p);
        goto again;
    }
    free(p);
}

// Variant 3: a pointer nulled before jumping back.
int variant3_nulled_before_goto(int *p) {
    if (!p) return 0;
    int s = 0;
again:
    s += *p;  // dtt-error {{null}}
    if (c()) {
        p = nullptr;
        goto again;
    }
    return s;
}

// Variant 4: a label inside a block, jumped to from after it.
void variant4_into_block() {
    int i = 0;
    if (c()) {
    inner:
        a[i] = 1;  // dtt-error {{cannot verify|out of bounds}}
        i++;
    }
    if (i < 100) goto inner;
}

int main() { return 0; }
