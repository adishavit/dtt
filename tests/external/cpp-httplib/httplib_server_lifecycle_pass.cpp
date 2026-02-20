// httplib::Server lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the Server lifecycle (construct -> configure/register -> listen -> stop)
// is tracked correctly by DTT ghost field @server_state via the ServerState enum.
// Each variant follows the proper state transitions.

#include <httplib.h>

// Variant 1: Basic server setup — construct, register handler, listen, stop
void variant_basic_lifecycle() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Get("/hello", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Hello World!", "text/plain");
    });  // requires @server_state == ServerState.VALID — satisfied

    svr.listen("0.0.0.0", 8080);  // requires VALID, ensures RUNNING
    svr.stop();                     // requires RUNNING, ensures STOPPED
}

// Variant 2: Multiple handler registrations before listen
void variant_multiple_handlers() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Get("/api/users", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("[]", "application/json");
    });

    svr.Post("/api/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 201;
    });

    svr.Put("/api/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    svr.Delete("/api/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // All registrations require VALID — satisfied for each call
    svr.listen("0.0.0.0", 9090);  // requires VALID, ensures RUNNING
    svr.stop();                     // requires RUNNING, ensures STOPPED
}

int main() {
    variant_basic_lifecycle();
    variant_multiple_handlers();
    return 0;
}
