// 832f_variant_else_branch_silent.cpp - Access in wrong else branch not caught
// DTT tracks else branches for 2-alternative variants but doesn't catch
// access to the WRONG type in else branch (silent = false negative)

#include <variant>
#include <string>

void test_get_wrong_type_in_else() {
    std::variant<int, std::string> v = 42;
    if (std::holds_alternative<int>(v)) {
        // OK: v holds int here
    } else {
        // v holds string here (not int)
        std::get<int>(v);  // BUG: v doesn't hold int in else - DTT should catch
    }
}

void test_get_wrong_index_in_else() {
    std::variant<int, double> v = 42;
    if (v.index() == 0) {
        // OK: index is 0 here
    } else {
        // index is 1 here (not 0)
        std::get<0>(v);  // BUG: index is not 0 in else - DTT should catch
    }
}

int main() {
    test_get_wrong_type_in_else();
    test_get_wrong_index_in_else();
    return 0;
}
