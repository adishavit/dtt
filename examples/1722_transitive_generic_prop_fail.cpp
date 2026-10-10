// 1722_transitive_generic_prop_fail.cpp
// Tests: Transitive propagation of generic property constraints
//
// Tests that the generalized fixpoint propagates ANY .dtt precondition,
// not just buffer sizes. Covers _nonnull across different function chains
// via Datalog-style fixpoint iteration.

#include <cstdio>
#include <cstring>

// === Non-null propagation through multiple wrappers (FILE* API) ===

static void check_eof(FILE* fp) {
    feof(fp);  // requires arg0._nonnull != 0
}

static void wrapper_a(FILE* fp) {
    check_eof(fp);
}

static void wrapper_b(FILE* fp) {
    wrapper_a(fp);
}

static void variant1_null_through_wrappers() {
    FILE* fp = nullptr;
    wrapper_b(fp);  // BUG: null through 2-level wrapper chain  dtt-error {{arg0\._nonnull  !=  0}}
}

// === Mixed: buffer size + null propagation ===

static void copy_data(FILE* fp, char* buf, size_t n) {
    fgetc(fp);         // requires arg0._nonnull != 0
    memcpy(buf, "x", n);  // requires buf._size_bytes >= n
}

static void mixed_wrapper(FILE* fp, char* buf, size_t n) {
    copy_data(fp, buf, n);  // Should propagate both requirements
}

static void variant2_null_fp_in_mixed() {
    char buf[100];
    FILE* fp = nullptr;
    mixed_wrapper(fp, buf, 10);  // BUG: null fp through wrapper  dtt-error {{arg0\._nonnull  !=  0}}
}

// === Four-level chain ===

// fgetc requires a stream; fflush(nullptr) deliberately flushes all streams.
static void level4(FILE* fp) { fgetc(fp); }
static void level3(FILE* fp) { level4(fp); }
static void level2(FILE* fp) { level3(fp); }
static void level1(FILE* fp) { level2(fp); }

static void variant3_four_level() {
    FILE* fp = nullptr;
    level1(fp);  // BUG: null through 4-level chain  dtt-error {{arg0\._nonnull  !=  0}}
}

int main() {
    variant1_null_through_wrappers();
    variant2_null_fp_in_mixed();
    variant3_four_level();
    return 0;
}
