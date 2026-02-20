// httplib::Server handler registration — all HTTP method handlers (should COMPILE)
//
// Exercises every handler registration method annotated in the DTT file:
//   Get(2), Post(2), Put(2), Patch(2), Delete(2), Options(2)
// All require @server_state == ServerState.VALID, which holds after construction.

#include <httplib.h>

// Variant 1: Register all six HTTP method handlers on a single server
void variant_all_handlers() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Get("/resource", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("GET response", "text/plain");
    });  // requires VALID -- satisfied

    svr.Post("/resource", [](const httplib::Request&, httplib::Response& res) {
        res.status = 201;
    });  // requires VALID -- satisfied

    svr.Put("/resource", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });  // requires VALID -- satisfied

    svr.Patch("/resource", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });  // requires VALID -- satisfied

    svr.Delete("/resource", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });  // requires VALID -- satisfied

    svr.Options("/resource", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Allow", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    });  // requires VALID -- satisfied

    svr.listen("0.0.0.0", 8080);  // requires VALID, ensures RUNNING
    svr.stop();                     // requires RUNNING, ensures STOPPED
}

// Variant 2: Patch and Options specifically (least-covered handlers)
void variant_patch_options() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Patch("/api/item", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"patched\":true}", "application/json");
    });

    svr.Options("/api/item", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Methods", "PATCH");
    });

    svr.listen("0.0.0.0", 9090);
    svr.stop();
}

// Variant 3: Multiple handlers for same method on different routes
void variant_multiple_routes() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Get("/users", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("[]", "application/json");
    });

    svr.Get("/posts", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("[]", "application/json");
    });

    svr.Post("/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 201;
    });

    svr.Post("/posts", [](const httplib::Request&, httplib::Response& res) {
        res.status = 201;
    });

    svr.Put("/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    svr.Patch("/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    svr.Delete("/users", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    svr.Options("/users", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Allow", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    });

    svr.listen("0.0.0.0", 7070);
    svr.stop();
}

// Variant 4: Request inspection methods (has_header, has_param, is_multipart_form_data)
void variant_request_inspection() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Post("/upload", [](const httplib::Request& req, httplib::Response& res) {
        if (req.has_header("Content-Type")) {
            // has_header: ensures_if_true _has_value == 1
        }

        if (req.has_param("name")) {
            // has_param: ensures_if_true _has_value == 1
        }

        if (req.is_multipart_form_data()) {
            // is_multipart_form_data: ensures_if_true _has_value == 1
            res.status = 200;
        }
    });

    svr.listen("0.0.0.0", 8080);
    svr.stop();
}

int main() {
    variant_all_handlers();
    variant_patch_options();
    variant_multiple_routes();
    variant_request_inspection();
    return 0;
}
