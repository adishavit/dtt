// httplib::Client lifecycle — correct usage patterns (should COMPILE)
//
// Tests that Client construction and HTTP method calls are tracked
// correctly by DTT ghost field @client_alive via the Nullable enum.
// Constructing a Client sets @client_alive = Nullable.VALID, and
// HTTP methods require @client_alive == Nullable.VALID.

#include <httplib.h>

// Variant 1: Basic GET request
void variant_basic_get() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto res = cli.Get("/hello");  // requires @client_alive == Nullable.VALID — satisfied
    (void)res;
}

// Variant 2: Multiple requests on same client
void variant_multiple_requests() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto res1 = cli.Get("/users");    // requires VALID — satisfied
    auto res2 = cli.Post("/users");   // requires VALID — satisfied
    auto res3 = cli.Delete("/users"); // requires VALID — satisfied

    (void)res1;
    (void)res2;
    (void)res3;
}

int main() {
    variant_basic_get();
    variant_multiple_requests();
    return 0;
}
