// 1721_transitive_resource_prop_fail.cpp
// Tests: Transitive propagation of resource-state preconditions from .dtt annotations
//
// When inner() calls fputc(fp), .dtt requires arg0._nonnull != 0.
// If outer() passes its parameter to inner(), outer() should inherit
// the _nonnull precondition transitively.
//
// DTT propagates .dtt requires through call chains via Datalog-style fixpoint.

#include <cstdio>

// Helper that requires a non-null, open FILE*
static void write_to_file(FILE* fp) {
    fputc('x', fp);  // .dtt: requires arg0._nonnull != 0
}

// Wrapper around write_to_file — should inherit _nonnull requirement
static void log_message(FILE* fp) {
    write_to_file(fp);
}

// Variant 1: Pass null through wrapper chain
static void variant1_null_through_chain() {
    FILE* fp = nullptr;
    log_message(fp);  // BUG: null propagated through chain
}

// Variant 2: Pass unchecked fopen through wrapper chain
static void variant2_unchecked_through_chain() {
    FILE* fp = fopen("/tmp/log.txt", "w");
    // Missing null check!
    log_message(fp);  // BUG: fp might be null
}

int main() {
    variant1_null_through_chain();
    variant2_unchecked_through_chain();
    return 0;
}
