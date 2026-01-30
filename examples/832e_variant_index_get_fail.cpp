// 832e_variant_index_get_fail.cpp - Index-based std::get not validated
// DTT catches std::get<Type> but NOT std::get<Index> (DTT now catches this)

#include <variant>

void test_get_wrong_index_simple() {
    std::variant<int, double> v = 42;  // index 0
    std::get<1>(v);  // BUG: v.index() is 0, not 1 - DTT should catch this
}

void test_get_wrong_index_after_assign() {
    std::variant<int, double, char> v = 'x';  // index 2
    std::get<0>(v);  // BUG: v.index() is 2, not 0
}

void test_get_index_in_wrong_switch_case() {
    std::variant<int, double> v = 42;  // index 0
    switch (v.index()) {
        case 0:
            std::get<1>(v);  // BUG: inside case 0 but accessing index 1
            break;
        default:
            break;
    }
}

void test_get_index_in_wrong_if_branch() {
    std::variant<int, double> v = 3.14;  // index 1
    if (v.index() == 1) {
        std::get<0>(v);  // BUG: proved index==1 but accessing index 0
    }
}

int main() {
    test_get_wrong_index_simple();
    test_get_wrong_index_after_assign();
    test_get_index_in_wrong_switch_case();
    test_get_index_in_wrong_if_branch();
    return 0;
}
