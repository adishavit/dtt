// s2n use-after-free on config — modify freed config (should FAIL to compile)
//
// Tests that DTT catches API calls on a freed config. After s2n_config_free,
// @config_alive == 0, but s2n_config_set_cipher_preferences requires
// @config_alive != 0.
//
// Related: Issue #5044 — leaked cert chain (same class of resource lifecycle bug)

#include <s2n.h>

// Variant 1: set_cipher_preferences after config_free — use-after-free
void variant_modify_freed_config() {
    struct s2n_config *config = s2n_config_new();  // @config_alive = 1
    if (!config) return;
    (void)s2n_config_free(config);                 // ensures @config_alive = 0

    // DTT ERROR: requires arg0.@config_alive != 0 (required state is not established in enforce mode)
    (void)s2n_config_set_cipher_preferences(config, "default_tls13");  // dtt-error {{requires @config_alive}}
}

int main() {
    variant_modify_freed_config();
    return 0;
}
