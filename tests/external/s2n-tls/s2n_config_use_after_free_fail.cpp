// s2n config operations after free -- multiple violations (should FAIL to compile)
//
// Tests that DTT catches various config API calls after s2n_config_free.
// After free, @config_alive == 0, but all config modification functions
// require @config_alive != 0.

#include <s2n.h>

// Variant 1: set_verification_ca_location on freed config
void variant_ca_location_after_free() {
    struct s2n_config *config = s2n_config_new();  // @config_alive = 1
    (void)s2n_config_free(config);                 // @config_alive = 0

    // DTT ERROR: requires arg0.@config_alive != 0, but @config_alive == 0
    (void)s2n_config_set_verification_ca_location(config, "/etc/ssl/certs", nullptr);
}

int main() {
    variant_ca_location_after_free();
    return 0;
}
