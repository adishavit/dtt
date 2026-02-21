// httplib::Server state machine — full lifecycle transitions (should COMPILE)
//
// Exercises the complete server state machine:
//   INVALID(0) -> VALID(1):   construction
//   VALID(1):                 handler registration, configuration
//   VALID(1) -> RUNNING(2):   listen()
//   RUNNING(2):               is_running() check
//   RUNNING(2) -> STOPPED(3): stop()
//
// Also tests is_running() and listen(3) (3-arg overload with socket_flags).

#include <httplib.h>

// Variant 1: Full state machine walk-through
void variant_full_state_machine() {
    httplib::Server svr;  // INVALID(0) -> VALID(1)

    // Configuration phase (state: VALID)
    svr.set_read_timeout(5, 0);
    svr.set_write_timeout(5, 0);
    svr.set_payload_max_length(1024 * 1024);

    // Handler registration phase (state: VALID)
    svr.Get("/api", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    svr.Post("/api", [](const httplib::Request&, httplib::Response& res) {
        res.status = 201;
    });

    // Transition to RUNNING
    svr.listen("0.0.0.0", 8080);  // VALID(1) -> RUNNING(2)

    // Transition to STOPPED
    svr.stop();  // RUNNING(2) -> STOPPED(3)
}

// Variant 2: listen with 3 arguments (socket_flags)
void variant_listen_three_args() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("healthy", "text/plain");
    });

    svr.listen("0.0.0.0", 9090, 0);  // listen(3): requires VALID, ensures RUNNING
    svr.stop();                        // requires RUNNING, ensures STOPPED
}

// Variant 3: is_valid check before full lifecycle
void variant_is_valid_then_lifecycle() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    if (svr.is_valid()) {
        svr.set_idle_interval(10, 0);
        svr.set_keep_alive_max_count(200);

        svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("Root", "text/plain");
        });

        svr.Patch("/item", [](const httplib::Request&, httplib::Response& res) {
            res.status = 200;
        });

        svr.Options("/item", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Allow", "GET, PATCH");
        });

        svr.listen("0.0.0.0", 7070);
        svr.stop();
    }
}

// Variant 4: Configuration + handlers + all HTTP methods before listen
void variant_comprehensive_setup() {
    httplib::Server svr;

    // All configuration
    svr.set_read_timeout(30, 0);
    svr.set_write_timeout(30, 0);
    svr.set_idle_interval(60, 0);
    svr.set_payload_max_length(1024 * 1024 * 10);
    svr.set_keep_alive_max_count(100);
    svr.set_base_dir("/www");
    svr.set_mount_point("/static", "/www/static");

    // All handlers
    svr.Get("/r", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("get", "text/plain");
    });
    svr.Post("/r", [](const httplib::Request&, httplib::Response& res) {
        res.status = 201;
    });
    svr.Put("/r", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });
    svr.Patch("/r", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });
    svr.Delete("/r", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });
    svr.Options("/r", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Allow", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    });

    // Lifecycle
    svr.listen("0.0.0.0", 5000, 0);
    svr.stop();
}

int main() {
    variant_full_state_machine();
    variant_listen_three_args();
    variant_is_valid_then_lifecycle();
    variant_comprehensive_setup();
    return 0;
}
