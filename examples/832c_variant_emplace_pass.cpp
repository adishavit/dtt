// 832c_variant_emplace_pass.cpp - Variant emplace tracking
// DTT tracks emplace<I>() and emplace<T>() and correctly sets _variant_index

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
