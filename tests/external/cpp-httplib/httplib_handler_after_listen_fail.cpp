// httplib::Server — handler registration after listen (should NOT compile)
//
// Tests that DTT catches handler registration when the server is in RUNNING state.
// All handler methods (Get, Post, Put, Patch, Delete, Options) require
// @server_state == ServerState.VALID, but listen() transitions to RUNNING.

#include <httplib.h>

// Variant 1: Post handler after listen
void variant_post_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8080);  // @server_state = ServerState.RUNNING

    svr.Post("/too-late", [](const httplib::Request&, httplib::Response& res) {  // dtt-error {{requires @server_state}}
        res.status = 201;
    });  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

// Variant 2: Patch handler after listen
void variant_patch_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8081);  // @server_state = ServerState.RUNNING

    svr.Patch("/late-patch", [](const httplib::Request&, httplib::Response& res) {  // dtt-error {{requires @server_state}}
        res.status = 200;
    });  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

// Variant 3: Options handler after listen
void variant_options_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8082);  // @server_state = ServerState.RUNNING

    svr.Options("/late-options", [](const httplib::Request&, httplib::Response& res) {  // dtt-error {{requires @server_state}}
        res.set_header("Allow", "GET");
    });  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

// Variant 4: Put handler after listen
void variant_put_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8083);  // @server_state = ServerState.RUNNING

    svr.Put("/late-put", [](const httplib::Request&, httplib::Response& res) {  // dtt-error {{requires @server_state}}
        res.status = 200;
    });  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

// Variant 5: Delete handler after listen
void variant_delete_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8084);  // @server_state = ServerState.RUNNING

    svr.Delete("/late-delete", [](const httplib::Request&, httplib::Response& res) {  // dtt-error {{requires @server_state}}
        res.status = 204;
    });  // DTT ERROR: requires VALID, but is RUNNING

    svr.stop();
}

int main() {
    variant_post_after_listen();
    variant_patch_after_listen();
    variant_options_after_listen();
    variant_put_after_listen();
    variant_delete_after_listen();
    return 0;
}
