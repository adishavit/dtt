// httplib::Result — unchecked access patterns (should NOT compile)
//
// Tests that DTT catches direct Result member access without first checking
// the Result via operator bool(). operator->() and operator*() require
// _nonnull == Nullable.VALID, which is only established by an if(res) check.

#include <httplib.h>

// Variant 1: Dereference without null check (operator*)
void variant_deref_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Get("/data");

    const auto& response = *res;  // DTT ERROR: requires _nonnull == Nullable.VALID, but unchecked
    (void)response;
}

// Variant 2: Arrow access on Post result without check
void variant_post_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Post("/submit", "{}", "application/json");

    auto status = res->status;  // DTT ERROR: requires _nonnull == Nullable.VALID
    (void)status;
}

// Variant 3: Arrow access on Put result without check
void variant_put_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Put("/update", "{}", "application/json");

    auto status = res->status;  // DTT ERROR: requires _nonnull == Nullable.VALID
    (void)status;
}

// Variant 4: Arrow access on Patch result without check
void variant_patch_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Patch("/modify", "{}", "application/json");

    auto body = res->body;  // DTT ERROR: requires _nonnull == Nullable.VALID
    (void)body;
}

// Variant 5: Arrow access on Delete result without check
void variant_delete_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Delete("/remove");

    auto status = res->status;  // DTT ERROR: requires _nonnull == Nullable.VALID
    (void)status;
}

// Variant 6: Arrow access on Head result without check
void variant_head_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Head("/check");

    auto status = res->status;  // DTT ERROR: requires _nonnull == Nullable.VALID
    (void)status;
}

// Variant 7: Arrow access on Options result without check
void variant_options_unchecked() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Options("/cors");

    auto status = res->status;  // DTT ERROR: requires _nonnull == Nullable.VALID
    (void)status;
}

int main() {
    variant_deref_unchecked();
    variant_post_unchecked();
    variant_put_unchecked();
    variant_patch_unchecked();
    variant_delete_unchecked();
    variant_head_unchecked();
    variant_options_unchecked();
    return 0;
}
