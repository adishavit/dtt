// 85_variant_getif_fail.cpp - Unsafe std::get_if usage
// DTT SHOULD catch this but doesn't yet (xfail = known gap)
// 

#include <variant>
#include <string>

int main() {
    // BUG: Dereferencing get_if result without null check
    std::variant<int, std::string> v = std::string{"hello"};
    auto* p = std::get_if<int>(&v);  // Returns nullptr since v holds string
    *p = 42;  // ERROR: dereferencing nullptr!  dtt-error {{potential null pointer dereference}}

    return 0;
}
