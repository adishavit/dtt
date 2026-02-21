// s2n double free connection -- double-free violation (should FAIL to compile)
//
// Tests that DTT catches s2n_connection_free called twice on the same
// connection. After the first free, @conn_state == 0, but operations
// on a freed connection are invalid.

#include <s2n.h>

// Variant 1: connection_free called twice
void variant_double_free_conn() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    (void)s2n_connection_free(conn);                               // @conn_state = 0

    // DTT ERROR: connection already freed (depends on how _nonnull is tracked)
    (void)s2n_connection_free(conn);
}

int main() {
    variant_double_free_conn();
    return 0;
}
