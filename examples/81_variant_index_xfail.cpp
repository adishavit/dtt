// 81_variant_index_xfail.cpp - Wrong std::variant index access
// DTT SHOULD catch this but doesn't yet (xfail = expected failure = known gap)
// When fixed, rename to 81_variant_index_fail.cpp

#include <variant>
#include <string>

int main() {
    // BUG: Access wrong index - will throw std::bad_variant_access at runtime
    std::variant<int, std::string> v = 42;  // v holds int (index 0)
    auto& s = std::get<std::string>(v);     // ERROR: v holds int, not string!
    (void)s;

    return 0;
}
