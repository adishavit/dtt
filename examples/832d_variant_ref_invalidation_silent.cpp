// 832d_variant_ref_invalidation_silent.cpp - Undetected dangling reference
// DTT SHOULD detect reference invalidation but doesn't yet (silent = false negative)
// Plan claims "Reference invalidation - Detect dangling refs after reassignment"

#include <variant>
#include <string>

void test_ref_invalidation_on_reassignment() {
    std::variant<int, std::string> v = 42;
    int& ref = std::get<int>(v);
    v = std::string("hello");  // Reassignment invalidates ref
    ref = 10;  // BUG: ref is dangling - DTT should catch this
}

void test_ref_invalidation_on_emplace() {
    std::variant<int, std::string> v = 42;
    int& ref = std::get<int>(v);
    v.emplace<std::string>("hello");  // Emplace invalidates ref
    ref = 10;  // BUG: ref is dangling - DTT should catch this
}

int main() {
    test_ref_invalidation_on_reassignment();
    test_ref_invalidation_on_emplace();
    return 0;
}
