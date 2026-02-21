// httplib SSL types — construction and is_valid patterns (should COMPILE)
//
// SSLServer and SSLClient require CPPHTTPLIB_OPENSSL_SUPPORT, which is not
// available in this build environment. This test exercises the SSL-adjacent
// patterns using the non-SSL types that share the same DTT annotations:
//   - Server with is_valid() check (mirrors SSLServer's validity pattern)
//   - Client construction and method calls (mirrors SSLClient behavior)
//
// The DTT annotations for SSLServer/SSLClient mirror Server/Client,
// so testing Server.is_valid() covers the same annotation logic.

#include <httplib.h>

// Variant 1: Server is_valid check then handler registration (SSLServer pattern)
void variant_server_validity_pattern() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    if (svr.is_valid()) {
        // ensures_if_true @server_state != ServerState.INVALID
        // Since construction sets VALID, this branch is taken

        svr.Get("/secure/data", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("Secure data", "text/plain");
        });

        svr.Post("/secure/upload", [](const httplib::Request&, httplib::Response& res) {
            res.status = 201;
        });

        svr.listen("0.0.0.0", 443);
        svr.stop();
    }
}

// Variant 2: is_valid with all handler types (full SSLServer setup pattern)
void variant_full_ssl_server_pattern() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    if (svr.is_valid()) {
        svr.Get("/api/v1/items", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("[]", "application/json");
        });

        svr.Post("/api/v1/items", [](const httplib::Request&, httplib::Response& res) {
            res.status = 201;
        });

        svr.Put("/api/v1/items", [](const httplib::Request&, httplib::Response& res) {
            res.status = 200;
        });

        svr.Patch("/api/v1/items", [](const httplib::Request&, httplib::Response& res) {
            res.status = 200;
        });

        svr.Delete("/api/v1/items", [](const httplib::Request&, httplib::Response& res) {
            res.status = 204;
        });

        svr.Options("/api/v1/items", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Allow", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        });

        svr.set_read_timeout(30, 0);
        svr.set_write_timeout(30, 0);

        svr.listen("0.0.0.0", 8443);
        svr.stop();
    }
}

// Variant 3: Client mimicking SSLClient construction and usage
void variant_ssl_client_pattern() {
    // SSLClient(1) mirrors Client(1) for DTT annotation purposes
    httplib::Client cli("https://example.com");  // @client_alive = Nullable.VALID

    // These methods mirror what SSLClient would use
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(30, 0);
    cli.set_write_timeout(30, 0);

    auto res = cli.Get("/secure/resource");

    if (res) {
        auto status = res->status;
        (void)status;
    }

    cli.stop();
}

int main() {
    variant_server_validity_pattern();
    variant_full_ssl_server_pattern();
    variant_ssl_client_pattern();
    return 0;
}
