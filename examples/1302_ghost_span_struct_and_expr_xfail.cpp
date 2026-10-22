// 1302_ghost_span_struct_and_expr_xfail.cpp
// Tests: Ghost span gaps — struct field bindings and stale vars in subexpressions.
// DTT ghost span binding currently only works for local VarDecl assignments
// and direct DeclRefExpr function arguments. These variants document remaining gaps.

#include <cstdlib>
#include <cstring>

void send_message(struct Message* msg);

// =============================================================================
// VARIANT 1: Stale length stored into struct field, propagated elsewhere
// Gap: DTT only creates ghost span bindings in VarDecl, not member assignments.
// msg.payload_len = n is a MemberExpr assignment, not a VarDecl.
// =============================================================================
struct Message {
    char* payload;
    size_t payload_len;  // Should track payload._size
};

void test_struct_propagation(size_t n) {
    Message msg;
    msg.payload = (char*)malloc(n);
    if (!msg.payload) return;
    msg.payload_len = n;

    // Reallocate payload but forget to update payload_len
    char* tmp = (char*)realloc(msg.payload, n / 2);
    if (!tmp) { free(msg.payload); return; }
    msg.payload = tmp;
    // msg.payload_len is still n — DESYNC!

    // Pass the whole struct — stale length propagates
    send_message(&msg);  // BUG: msg.payload_len > actual payload size

    free(msg.payload);
}

// =============================================================================
// VARIANT 2: Stale length in subexpression of function argument
// Gap: DTT only checks direct DeclRefExpr arguments, not nested expressions.
// realloc(buf, len * 2) — len is inside a BinaryOperator, not a bare DeclRefExpr.
// =============================================================================
void test_realloc_from_stale_length(size_t n) {
    char* buf = (char*)malloc(n);
    if (!buf) return;
    size_t len = n;

    // First grow
    char* tmp = (char*)realloc(buf, n * 2);
    if (!tmp) { free(buf); return; }
    buf = tmp;
    // buf._size = n*2, len = n — DESYNC!

    // "Double the buffer" using stale len in subexpression
    tmp = (char*)realloc(buf, len * 2);  // BUG: reallocates to 2n, which is SAME
    if (!tmp) { free(buf); return; }     // as current size, not 4n as intended
    buf = tmp;

    // Developer thinks buf is now 4n, but it's only 2n
    // Any access in range [2n, 4n) would be OOB
    free(buf);
}

int main() { return 0; }
