// 832c_variant_emplace_xfail.cpp - Untracked variant emplace
// DTT SHOULD track emplace but doesn't yet (xfail = known gap)
// Plan claims "emplace tracking - Update index on v.emplace<I>()"

#include <variant>
#include <string>

void test_emplace_by_index() {
    std::variant<int, std::string> v;
    v.emplace<1>("hello");  // DTT loses tracking here
    std::get<1>(v);  // ERROR: DTT should allow this (index is known to be 1)
}

void test_emplace_by_type() {
    std::variant<int, std::string> v;
    v.emplace<std::string>("hello");  // DTT loses tracking here
    std::get<std::string>(v);  // ERROR: DTT should allow this (holds string)
}

int main() {
    test_emplace_by_index();
    test_emplace_by_type();
    return 0;
}
