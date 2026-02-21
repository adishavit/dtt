// httplib::Server configuration — all config methods before listen (should COMPILE)
//
// Exercises every server configuration method annotated in the DTT file:
//   set_read_timeout(2), set_write_timeout(2), set_idle_interval(2),
//   set_payload_max_length(1), set_keep_alive_max_count(1),
//   set_mount_point(2), set_base_dir(1), set_base_dir(2),
//   set_file_extension_and_mimetype_mapping, set_exception_handler,
//   set_error_handler, set_pre_routing_handler, set_post_routing_handler,
//   set_logger, set_keep_alive_timeout
// All require @server_state == ServerState.VALID.

#include <httplib.h>

// Variant 1: Timeout and keep-alive configuration
void variant_timeouts() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.set_read_timeout(5, 0);         // requires VALID -- satisfied
    svr.set_write_timeout(5, 0);        // requires VALID -- satisfied
    svr.set_idle_interval(10, 0);       // requires VALID -- satisfied
    svr.set_keep_alive_max_count(100);  // requires VALID -- satisfied

    svr.listen("0.0.0.0", 8080);
    svr.stop();
}

// Variant 2: Payload and base directory configuration
void variant_payload_and_dirs() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.set_payload_max_length(1024 * 1024 * 16);  // requires VALID -- satisfied
    svr.set_base_dir("/var/www");                    // requires VALID -- satisfied (1-arg)
    svr.set_mount_point("/static", "/var/www/static"); // requires VALID -- satisfied

    svr.listen("0.0.0.0", 8081);
    svr.stop();
}

// Variant 3: set_base_dir with mount point (2-arg overload)
void variant_base_dir_two_args() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.set_base_dir("/var/www", "/mount");  // requires VALID -- satisfied (2-arg)

    svr.listen("0.0.0.0", 8082);
    svr.stop();
}

// Variant 4: Handler/callback configuration
void variant_handlers_config() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_content("Error", "text/plain");
    });

    svr.set_exception_handler(
        [](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
            res.status = 500;
            res.set_content("Internal Error", "text/plain");
        });

    svr.set_pre_routing_handler(
        [](const httplib::Request&, httplib::Response&)
            -> httplib::Server::HandlerResponse {
            return httplib::Server::HandlerResponse::Unhandled;
        });

    svr.set_post_routing_handler(
        [](const httplib::Request&, httplib::Response&) {
        });

    svr.set_logger([](const httplib::Request&, const httplib::Response&) {
    });

    svr.listen("0.0.0.0", 8083);
    svr.stop();
}

// Variant 5: File extension mapping and mime type config
void variant_mimetype_config() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.set_file_extension_and_mimetype_mapping("json", "application/json");
    svr.set_file_extension_and_mimetype_mapping("wasm", "application/wasm");

    svr.listen("0.0.0.0", 8084);
    svr.stop();
}

// Variant 6: All configuration methods in a single setup
void variant_full_config() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.set_read_timeout(30, 0);
    svr.set_write_timeout(30, 0);
    svr.set_idle_interval(60, 0);
    svr.set_payload_max_length(1024 * 1024);
    svr.set_keep_alive_max_count(50);
    svr.set_base_dir("/www");
    svr.set_mount_point("/assets", "/www/assets");
    svr.set_file_extension_and_mimetype_mapping("svg", "image/svg+xml");

    svr.set_logger([](const httplib::Request&, const httplib::Response&) {});

    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_content("Error", "text/plain");
    });

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    svr.listen("0.0.0.0", 8085);
    svr.stop();
}

int main() {
    variant_timeouts();
    variant_payload_and_dirs();
    variant_base_dir_two_args();
    variant_handlers_config();
    variant_mimetype_config();
    variant_full_config();
    return 0;
}
