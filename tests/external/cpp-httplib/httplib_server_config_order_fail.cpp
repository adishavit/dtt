// httplib::Server configuration order — config after listen (should NOT compile)
//
// Tests that DTT catches server configuration and handler registration
// after listen() has been called. Configuration requires @server_state == VALID,
// but listen() transitions the state to RUNNING.

#include <httplib.h>

// Variant 1: set_read_timeout after listen
void variant_timeout_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8080);  // @server_state = ServerState.RUNNING

    svr.set_read_timeout(5, 0);  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

// Variant 2: Register Get handler after listen
void variant_handler_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8080);  // @server_state = ServerState.RUNNING

    svr.Get("/too-late", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Registered too late", "text/plain");
    });  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

int main() {
    variant_timeout_after_listen();
    variant_handler_after_listen();
    return 0;
}
