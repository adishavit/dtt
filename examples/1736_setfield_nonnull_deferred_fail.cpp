// 1736: setFieldTyped _nonnull should trigger deferred guard evaluation
// When _nonnull is set via setFieldTyped (e.g., from inferNonnullFromExpr
// on initialization), it should trigger pending deferred postconditions.

#include <cstdio>

// Variant 1: early return null check triggers deferred eval → leak detected
void variant1_early_return_leak() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;
    // @open = 1 now (deferred eval triggered by _nonnull proof)
    // forgot fclose
    // ERROR: leak
}

// Variant 2: resource properly closed after null check (control)
void variant2_early_return_closed() {
    FILE* f = fopen("data.txt", "r");
    if (!f) return;
    fclose(f);
    // OK: no leak
}

int main() { return 0; }
