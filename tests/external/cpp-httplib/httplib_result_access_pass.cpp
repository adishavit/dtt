// httplib::Result operations — operator bool, operator->, operator*, error (should COMPILE)
//
// Exercises all Result access methods annotated in the DTT file:
//   operator bool(0): ensures_if_true _nonnull == Nullable.VALID
//   operator*(0):     requires _nonnull == Nullable.VALID
//   operator->(0):    requires _nonnull == Nullable.VALID
//   error(0):         requires _nonnull == Nullable.NULL_PTR

#include <httplib.h>

// Variant 1: operator bool + operator-> (arrow access after null check)
void variant_arrow_access() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Get("/data");

    if (res) {  // operator bool: ensures_if_true _nonnull == Nullable.VALID
        int status = res->status;  // operator->: requires _nonnull == Nullable.VALID
        (void)status;
    }
}

// Variant 2: operator bool + operator* (dereference after null check)
void variant_deref_access() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Post("/submit", "{}", "application/json");

    if (res) {  // operator bool: _nonnull == Nullable.VALID
        const auto& response = *res;  // operator*: requires _nonnull == Nullable.VALID
        (void)response;
    }
}

// Variant 3: error() in the false/null branch
void variant_error_on_failure() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Get("/might-fail");

    if (!res) {  // operator bool: ensures_if_false _nonnull == Nullable.NULL_PTR
        auto err = res.error();  // error(): requires _nonnull == Nullable.NULL_PTR
        (void)err;
    }
}

// Variant 4: Both branches -- success and failure handling
void variant_both_branches() {
    httplib::Client cli("http://localhost:8080");

    auto res = cli.Get("/api/resource");

    if (res) {
        // Success path: _nonnull == Nullable.VALID
        int status = res->status;        // operator-> OK
        const auto& body = (*res).body;  // operator* OK
        (void)status;
        (void)body;
    } else {
        // Failure path: _nonnull == Nullable.NULL_PTR
        auto err = res.error();  // error() OK
        (void)err;
    }
}

// Variant 5: Multiple Result checks in sequence
void variant_sequential_checks() {
    httplib::Client cli("http://localhost:8080");

    auto res1 = cli.Get("/first");
    if (res1) {
        auto s = res1->status;
        (void)s;
    }

    auto res2 = cli.Post("/second");
    if (res2) {
        const auto& r = *res2;
        (void)r;
    } else {
        auto e = res2.error();
        (void)e;
    }

    auto res3 = cli.Put("/third", "{}", "application/json");
    if (res3) {
        auto s = res3->status;
        (void)s;
    }
}

// Variant 6: Result from Head, Patch, Delete, Options
void variant_result_from_all_methods() {
    httplib::Client cli("http://localhost:8080");

    auto r1 = cli.Head("/check");
    if (r1) {
        auto s = r1->status;
        (void)s;
    }

    auto r2 = cli.Patch("/update", "{}", "application/json");
    if (r2) {
        auto s = r2->status;
        (void)s;
    }

    auto r3 = cli.Delete("/remove");
    if (r3) {
        auto s = r3->status;
        (void)s;
    }

    auto r4 = cli.Options("/cors");
    if (r4) {
        auto s = r4->status;
        (void)s;
    } else {
        auto e = r4.error();
        (void)e;
    }
}

int main() {
    variant_arrow_access();
    variant_deref_access();
    variant_error_on_failure();
    variant_both_branches();
    variant_sequential_checks();
    variant_result_from_all_methods();
    return 0;
}
