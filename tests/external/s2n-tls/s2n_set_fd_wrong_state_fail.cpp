// s2n set_fd in wrong state -- state machine violation (should FAIL to compile)
//
// Tests that DTT catches s2n_connection_set_fd called after the connection
// has already been configured (set_fd already called). After set_fd,
// @conn_state == 2, but set_fd requires @conn_state == 1 (created).

#include <s2n.h>

// Variant 1: set_fd called twice -- connection already configured
void variant_set_fd_twice() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    if (!conn) return;

    (void)s2n_connection_set_fd(conn, 3);                          // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 1 (required state is not established in enforce mode)
    (void)s2n_connection_set_fd(conn, 4);  // dtt-error {{requires @conn_state}}
}

int main() {
    variant_set_fd_twice();
    return 0;
}
