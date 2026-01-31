// 83_variant_holds_fail.cpp - Unguarded variant access
// DTT SHOULD catch these but doesn't yet (xfail = known gap)
// 

#include <variant>
#include <string>

int main() {
    // BUG: Access without any guard on unknown variant
    std::variant<int, std::string> v;  // Could be either (default is int, but might change)

    // This could throw at runtime - no guard to prove v holds string
    auto& s = std::get<std::string>(v);  // ERROR: unguarded access
    (void)s;

    return 0;
}
