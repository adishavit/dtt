// s2n config lifecycle — double free (should FAIL to compile)
//
// Tests that DTT catches double-free of an s2n_config. After the first
// s2n_config_free, @config_alive == 0, but the second free requires
// @config_alive != 0.
//
// Related: Issue #3525 — leaked connection on error path (same class of
// resource-management bug)

#include <s2n.h>

// Variant 1: config_free called twice — classic double-free
void variant_double_free() {
    struct s2n_config *config = s2n_config_new();  // @config_alive = 1
    if (!config) return;
    (void)s2n_config_free(config);                 // ensures @config_alive = 0

    // DTT ERROR: requires arg0.@config_alive != 0 (required state is not established in enforce mode)
    (void)s2n_config_free(config);  // dtt-error {{requires @config_alive}}
}

int main() {
    variant_double_free();
    return 0;
}
