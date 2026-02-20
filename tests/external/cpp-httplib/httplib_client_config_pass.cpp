// httplib::Client configuration — all client config methods (should COMPILE)
//
// Exercises every Client configuration method annotated in the DTT file:
//   set_connection_timeout(2), set_read_timeout(2), set_write_timeout(2),
//   set_keep_alive(1), set_follow_location(1), set_basic_auth(2),
//   set_bearer_token_auth(1), set_proxy(2)
// All require @client_alive == Nullable.VALID.

#include <httplib.h>

// Variant 1: Timeout configuration
void variant_timeouts() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    cli.set_connection_timeout(5, 0);  // requires VALID -- satisfied
    cli.set_read_timeout(30, 0);       // requires VALID -- satisfied
    cli.set_write_timeout(30, 0);      // requires VALID -- satisfied

    auto res = cli.Get("/data");
    (void)res;
}

// Variant 2: Keep-alive and redirect
void variant_keepalive_redirect() {
    httplib::Client cli("http://localhost:8080");

    cli.set_keep_alive(true);        // requires VALID -- satisfied
    cli.set_follow_location(true);   // requires VALID -- satisfied

    auto res = cli.Get("/redirected");
    (void)res;
}

// Variant 3: Authentication methods
void variant_auth() {
    httplib::Client cli("http://localhost:8080");

    cli.set_basic_auth("admin", "secret");   // requires VALID -- satisfied

    auto res1 = cli.Get("/admin/dashboard");
    (void)res1;

    httplib::Client cli2("http://localhost:8080");
    cli2.set_bearer_token_auth("eyJhbGciOiJIUzI1NiJ9.token");  // requires VALID

    auto res2 = cli2.Get("/api/protected");
    (void)res2;
}

// Variant 4: Proxy configuration
void variant_proxy() {
    httplib::Client cli("http://external-api.com");

    cli.set_proxy("proxy.corp.local", 3128);  // requires VALID -- satisfied

    auto res = cli.Get("/external/data");
    (void)res;
}

// Variant 5: All configuration together, then multiple requests
void variant_full_config_then_requests() {
    httplib::Client cli("localhost", 8080);  // Client(2)

    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(60, 0);
    cli.set_write_timeout(60, 0);
    cli.set_keep_alive(true);
    cli.set_follow_location(true);
    cli.set_basic_auth("user", "pass");
    cli.set_proxy("proxy.local", 8888);

    auto r1 = cli.Get("/api/items");
    auto r2 = cli.Post("/api/items", "{\"name\":\"new\"}", "application/json");
    auto r3 = cli.Head("/api/items");

    (void)r1; (void)r2; (void)r3;

    cli.stop();  // requires VALID -- satisfied
}

int main() {
    variant_timeouts();
    variant_keepalive_redirect();
    variant_auth();
    variant_proxy();
    variant_full_config_then_requests();
    return 0;
}
