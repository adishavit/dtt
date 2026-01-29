// 85_variant_getif_xfail.cpp - Unsafe std::get_if usage
// DTT SHOULD catch this but doesn't yet (xfail = known gap)
// When fixed, rename to 85_variant_getif_fail.cpp

#include <variant>
#include <string>

int main() {
    // BUG: Dereferencing get_if result without null check
    std::variant<int, std::string> v = std::string{"hello"};
    auto* p = std::get_if<int>(&v);  // Returns nullptr since v holds string
    *p = 42;  // ERROR: dereferencing nullptr!

    return 0;
}
