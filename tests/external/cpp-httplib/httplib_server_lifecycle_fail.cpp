// httplib::Server lifecycle — incorrect state transitions (should NOT compile)
//
// Tests that DTT catches server operations performed in the wrong state.
// The @server_state ghost field must follow:
//   VALID -> (listen) -> RUNNING -> (stop) -> STOPPED

#include <httplib.h>

// Variant 1: Stop before listen — server is VALID, not RUNNING
void variant_stop_before_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Hello", "text/plain");
    });

    svr.stop();  // DTT ERROR: requires @server_state == ServerState.RUNNING, but is VALID  dtt-error {{requires @server_state}}
}

// Variant 2: Operations on a stopped server
void variant_ops_on_stopped() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8080);  // @server_state = ServerState.RUNNING
    svr.stop();                     // @server_state = ServerState.STOPPED

    svr.Get("/late", [](const httplib::Request&, httplib::Response& res) {  // dtt-error {{requires @server_state}}
        res.set_content("Too late", "text/plain");
    });  // DTT ERROR: requires @server_state == ServerState.VALID, but is STOPPED
}

int main() {
    variant_stop_before_listen();
    variant_ops_on_stopped();
    return 0;
}
