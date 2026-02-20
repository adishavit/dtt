// s2n cert chain operations after free -- use-after-free (should FAIL to compile)
//
// Tests that DTT catches cert chain API calls after s2n_cert_chain_and_key_free.
// After free, @cert_alive == 0, but cert operations require @cert_alive != 0.

#include <s2n.h>

// Variant 1: load_pem on a freed cert chain
void variant_load_pem_after_free() {
    struct s2n_cert_chain_and_key *cert = s2n_cert_chain_and_key_new();  // @cert_alive = 1
    (void)s2n_cert_chain_and_key_free(cert);                             // @cert_alive = 0

    // DTT ERROR: requires arg0.@cert_alive != 0, but @cert_alive == 0
    (void)s2n_cert_chain_and_key_load_pem(cert, "cert.pem", "key.pem");
}

int main() {
    variant_load_pem_after_free();
    return 0;
}
