// Safe variant access is incorrectly rejected: switch cases do not establish
// per-case index proofs. This is a known false positive, not a detection test.

#include <variant>
#include <string>

void test_switch_get_by_index() {
    std::variant<int, double, std::string> v = 3.14;  // index 1
    switch (v.index()) {
        case 0: std::get<0>(v); break;  // Should be safe: proved index==0 in case 0
        case 1: std::get<1>(v); break;  // Should be safe: proved index==1 in case 1
        case 2: std::get<2>(v); break;  // Should be safe: proved index==2 in case 2
    }
}

void test_switch_get_by_type() {
    std::variant<int, double, std::string> v = 3.14;  // index 1
    switch (v.index()) {
        case 0: std::get<int>(v); break;         // Should be safe
        case 1: std::get<double>(v); break;      // Should be safe
        case 2: std::get<std::string>(v); break; // Should be safe
    }
}

int main() {
    test_switch_get_by_index();
    test_switch_get_by_type();
    return 0;
}
