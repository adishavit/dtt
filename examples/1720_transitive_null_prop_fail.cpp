// 1720_transitive_null_prop_fail.cpp
// Tests: Transitive propagation of null-check preconditions from .dtt annotations
//
// When inner() calls fgetc(fp), .dtt requires arg0._nonnull != 0.
// If outer() passes its parameter to inner(), outer() should inherit
// the _nonnull precondition transitively.
//
// DTT propagates .dtt requires through call chains via Datalog-style fixpoint.

#include <cstdio>

// Level 1: Calls fgetc which requires arg0._nonnull != 0 per libc.dtt
static void read_char(FILE* fp) {
    fgetc(fp);  // .dtt: requires arg0._nonnull != 0
}

// Level 0: Calls read_char, passing its own parameter through.
// Should inherit: arg0._nonnull != 0 from read_char's requirement.
static void process_file(FILE* fp) {
    read_char(fp);  // Should propagate: fp._nonnull != 0
}

// Variant 1: Direct null — should be caught transitively
static void variant1_direct_null() {
    FILE* fp = nullptr;
    process_file(fp);  // BUG: fp is null, should be caught transitively  dtt-error {{arg0\._nonnull  !=  0}}
}

// Variant 2: Unchecked fopen result — should be caught transitively
static void variant2_unchecked_fopen() {
    FILE* fp = fopen("/tmp/test.txt", "r");
    // Missing null check! fopen can return NULL.
    process_file(fp);  // KNOWN GAP (not asserted): fp might be null
}

// Variant 3: Three-level chain
static void deep_read(FILE* fp) {
    fgetc(fp);  // requires _nonnull
}

static void deep_wrapper(FILE* fp) {
    deep_read(fp);
}

static void deep_outer(FILE* fp) {
    deep_wrapper(fp);
}

static void variant3_deep_chain() {
    FILE* fp = nullptr;
    deep_outer(fp);  // BUG: null through 3-level chain  dtt-error {{arg0\._nonnull  !=  0}}
}

int main() {
    variant1_direct_null();
    variant2_unchecked_fopen();
    variant3_deep_chain();
    return 0;
}
