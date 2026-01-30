// 91_any_type_fail.cpp - Unsafe std::any_cast type mismatch
// DTT SHOULD catch this but doesn't yet (xfail = known gap)
// 

#include <any>
#include <string>

int main() {
    // BUG: Casting to wrong type - will throw std::bad_any_cast at runtime
    std::any a = 42;  // a holds int
    auto s = std::any_cast<std::string>(a);  // ERROR: a holds int, not string!
    (void)s;

    return 0;
}
