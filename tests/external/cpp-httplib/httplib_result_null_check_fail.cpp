// httplib::Result null check — access without check (should NOT compile)
//
// Tests that DTT catches direct access to Result members without
// first checking if the result is valid via operator bool().
// operator->() requires _nonnull == Nullable.VALID, which is only
// established by a preceding if(res) check.

#include <httplib.h>

// Variant 1: Access res->status without null check
void variant_unchecked_access() {
    httplib::Client cli("http://localhost:8080");  // @client_alive = Nullable.VALID

    auto res = cli.Get("/data");  // returns Result, _nonnull is unknown

    auto status = res->status;  // DTT ERROR: requires _nonnull == Nullable.VALID, but unchecked
    (void)status;
}

int main() {
    variant_unchecked_access();
    return 0;
}
