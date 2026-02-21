// httplib::Result null check — correct access after check (should COMPILE)
//
// Tests that DTT tracks the Result nullable wrapper correctly.
// operator bool() sets _nonnull == Nullable.VALID when true,
// and operator->() / operator*() require _nonnull == Nullable.VALID.

#include <httplib.h>

// Variant 1: if(res) guard before arrow access
void variant_if_guard() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto res = cli.Get("/data");  // returns Result

    if (res) {  // operator bool: ensures_if_true _nonnull == Nullable.VALID
        auto status = res->status;  // requires _nonnull == Nullable.VALID — satisfied
        (void)status;
    }
}

// Variant 2: Explicit operator bool check with dereference
void variant_explicit_bool() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto res = cli.Get("/info");  // returns Result

    if (res) {  // operator bool: ensures_if_true _nonnull == Nullable.VALID
        const auto& response = *res;  // operator*: requires _nonnull == Nullable.VALID — satisfied
        (void)response;
    }
}

int main() {
    variant_if_guard();
    variant_explicit_bool();
    return 0;
}
