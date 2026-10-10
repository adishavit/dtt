// Test: arity mismatch warning when function name matches .dtt but arity differs
//
// guarded_deref is annotated in test_custom.dtt as guarded_deref(1).
// Calling a differently-arited version should emit a warning.

void guarded_deref(int* p, int extra);  // 2 args, but .dtt has guarded_deref(1)

void variant_wrong_arity() {
    int x = 42;
    guarded_deref(&x, 0);  // should warn: annotated for 1 arg, called with 2  dtt-error {{called with 2 args but annotated for 1}}
}
