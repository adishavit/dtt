// 1731: Smarter ghost field merge — resource leaked on one branch
// When both if-branches execute (neither terminates), a ghost field
// set via deferred eval in the then-branch should still be tracked
// at the merge point.

#include <cstdio>

// Variant 1: if (f) { use but forget close } else { ... }
void variant1_if_else_leak() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    if (f) {
        int c = fgetc(f);
        (void)c;
        // forgot fclose(f)
    } else {
        // null path
    }
    // ERROR: f.@open may be 1, needs fclose
}

// Variant 2: close on one branch only
void variant2_partial_close() {
    FILE* f = fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    if (!f) return;
    int x = 0;
    if (x) {
        fclose(f);
    }
    // ERROR: f may not be closed
}

int main() { return 0; }
