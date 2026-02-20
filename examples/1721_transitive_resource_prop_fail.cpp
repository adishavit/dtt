// 1721_transitive_resource_prop_xfail.cpp
// Tests: Transitive propagation of resource-state preconditions from .dtt annotations
//
// When inner() calls fclose(fp), .dtt requires arg0._nonnull != 0.
// And when inner() calls fgetc after fclose, .dtt requires arg0._nonnull != 0
// but the resource is closed. These constraints should propagate transitively.
//
// Currently DTT does NOT propagate .dtt requires through call chains.
// This is an xfail test documenting the gap.
//
// When fixed: rename to 1721_transitive_resource_prop_fail.cpp

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
