// 833b_any_else_branch_fail.cpp - Access in wrong else branch after type check
// DTT correctly catches wrong type access in else branch after a.type() == typeid(T)

#include <any>
#include <typeinfo>

void test_cast_wrong_type_in_else() {
    std::any a = 42;
    if (a.type() == typeid(int)) {
        // OK: a holds int here
    } else {
        // a does NOT hold int here
        std::any_cast<int>(a);  // BUG: a doesn't hold int in else - DTT should catch
    }
}

int main() {
    test_cast_wrong_type_in_else();
    return 0;
}
