// httplib::Client operations — all HTTP methods and config (should COMPILE)
//
// Exercises every Client method annotated in the DTT file:
//   Client(1), Client(2)
//   Get(1), Get(2), Get(3), Head(1), Post(1), Post(3), Put(3), Patch(3),
//   Delete(1), Delete(3), Options(1), stop()
//   set_connection_timeout(2), set_read_timeout(2), set_write_timeout(2),
//   set_keep_alive(1), set_follow_location(1), set_basic_auth(2),
//   set_bearer_token_auth(1), set_proxy(2)
// All require @client_alive == Nullable.VALID.

#include <httplib.h>

// Variant 1: All HTTP methods with 1-arg paths
void variant_all_methods_simple() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto r1 = cli.Get("/data");      // requires VALID -- satisfied
    auto r2 = cli.Head("/data");     // requires VALID -- satisfied
    auto r3 = cli.Post("/data");     // requires VALID -- satisfied
    auto r4 = cli.Delete("/data");   // requires VALID -- satisfied
    auto r5 = cli.Options("/data");  // requires VALID -- satisfied

    (void)r1; (void)r2; (void)r3; (void)r4; (void)r5;
}

// Variant 2: Get with headers (2-arg overload)
void variant_get_with_headers() {
    httplib::Client cli("http://localhost:8080");

    httplib::Headers hdrs = {{"Authorization", "Bearer token123"}};
    auto res = cli.Get("/protected", hdrs);  // Get(2): requires VALID -- satisfied

    (void)res;
}

// Variant 3: Post with body (3-arg overload: path, body, content_type)
void variant_post_with_body() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Post("/api/items", "{\"name\":\"item1\"}", "application/json");
    // Post(3): requires VALID -- satisfied

    (void)res;
}

// Variant 4: Put with body (3-arg overload: path, body, content_type)
void variant_put_with_body() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Put("/api/items", "{\"name\":\"updated\"}", "application/json");
    // Put(3): requires VALID -- satisfied

    (void)res;
}

// Variant 5: Patch with body (3-arg overload: path, body, content_type)
void variant_patch_with_body() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Patch("/api/items", "{\"status\":\"done\"}", "application/json");
    // Patch(3): requires VALID -- satisfied

    (void)res;
}

// Variant 6: Delete with body (3-arg overload: path, body, content_type)
void variant_delete_with_body() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Delete("/api/items", "{\"id\":42}", "application/json");
    // Delete(3): requires VALID -- satisfied

    (void)res;
}

// Variant 7: Client with host and port (2-arg constructor)
void variant_client_two_arg_ctor() {
    httplib::Client cli("localhost", 9090);  // Client(2): @client_alive = Nullable.VALID

    auto res = cli.Get("/status");  // requires VALID -- satisfied
    (void)res;
}

// Variant 8: Client stop
void variant_client_stop() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto res = cli.Get("/data");  // requires VALID -- satisfied
    (void)res;

    cli.stop();  // requires VALID -- satisfied
}

// Variant 9: All client configuration methods
void variant_client_config() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    cli.set_connection_timeout(5, 0);   // requires VALID -- satisfied
    cli.set_read_timeout(30, 0);        // requires VALID -- satisfied
    cli.set_write_timeout(30, 0);       // requires VALID -- satisfied
    cli.set_keep_alive(true);           // requires VALID -- satisfied
    cli.set_follow_location(true);      // requires VALID -- satisfied
    cli.set_basic_auth("user", "pass"); // requires VALID -- satisfied
    cli.set_bearer_token_auth("tok");   // requires VALID -- satisfied
    cli.set_proxy("proxy.local", 3128); // requires VALID -- satisfied

    auto res = cli.Get("/configured");
    (void)res;
}

// Variant 10: Full client workflow with config, multiple methods, and stop
void variant_full_client_workflow() {
    httplib::Client cli("localhost", 8080);  // Client(2)

    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(30, 0);
    cli.set_write_timeout(30, 0);
    cli.set_keep_alive(true);
    cli.set_follow_location(true);

    auto r1 = cli.Get("/api/v1/items");
    auto r2 = cli.Post("/api/v1/items", "{}", "application/json");
    auto r3 = cli.Put("/api/v1/items", "{}", "application/json");
    auto r4 = cli.Patch("/api/v1/items", "{}", "application/json");
    auto r5 = cli.Delete("/api/v1/items");
    auto r6 = cli.Head("/api/v1/items");
    auto r7 = cli.Options("/api/v1/items");

    (void)r1; (void)r2; (void)r3; (void)r4; (void)r5; (void)r6; (void)r7;

    cli.stop();
}

int main() {
    variant_all_methods_simple();
    variant_get_with_headers();
    variant_post_with_body();
    variant_put_with_body();
    variant_patch_with_body();
    variant_delete_with_body();
    variant_client_two_arg_ctor();
    variant_client_stop();
    variant_client_config();
    variant_full_client_workflow();
    return 0;
}
