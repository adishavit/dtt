// httplib::SSLServer with is_valid() check — correct pattern (should COMPILE)
//
// Tests that SSLServer lifecycle is tracked correctly. SSLServer construction
// may fail (e.g., bad cert/key paths), so the is_valid() check pattern is
// the recommended usage. is_valid() with ensures_if_true establishes that
// @server_state != ServerState.INVALID within the true branch.
//
// NOTE: CPPHTTPLIB_OPENSSL_SUPPORT=0 is set, so SSLServer is not available.
// This test uses Server with the is_valid() check pattern instead, which
// exercises the same DTT annotation logic.

#include <httplib.h>

// Variant 1: Server with is_valid() check before listen
void variant_is_valid_check() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    if (svr.is_valid()) {  // ensures_if_true @server_state != ServerState.INVALID
        svr.Get("/secure", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("OK", "text/plain");
        });  // requires @server_state == ServerState.VALID — satisfied

        svr.listen("0.0.0.0", 443);  // requires VALID, ensures RUNNING
        svr.stop();                    // requires RUNNING, ensures STOPPED
    }
}

int main() {
    variant_is_valid_check();
    return 0;
}
