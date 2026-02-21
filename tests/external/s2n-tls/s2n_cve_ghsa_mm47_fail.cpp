// CVE GHSA-mm47: null-pointer dereference on connection (should FAIL to compile)
//
// GHSA-mm47 involves a NULL-pointer dereference when a connection is used
// without proper initialization. DTT catches this via the _nonnull
// precondition on s2n_connection_set_config's config argument: passing a
// null config pointer violates requires arg1._nonnull != 0.
//
// Reference: https://github.com/aws/s2n-tls/security/advisories/GHSA-mm47
// Related: Issue #4752 — missing null check before use

#include <s2n.h>

// Variant 1: null config passed to set_config
void variant_null_config() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1

    // DTT ERROR: requires arg1._nonnull != 0, but config is null
    (void)s2n_connection_set_config(conn, (struct s2n_config *)0);
}

int main() {
    variant_null_config();
    return 0;
}
