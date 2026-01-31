// 84_variant_getif_pass.cpp - Safe std::get_if patterns
// All accesses here use null-checking - DTT should NOT error

#include <variant>
#include <string>

int main() {
    // Variant 1: get_if with null check in if statement
    {
        std::variant<int, std::string> v = 42;
        if (auto* p = std::get_if<int>(&v)) {
            *p = 100;  // OK: null-checked
        }
    }

    // Variant 2: get_if with null check - string case
    {
        std::variant<int, std::string> v = std::string{"hello"};
        if (auto* s = std::get_if<std::string>(&v)) {
            s->append(" world");  // OK: null-checked
        }
    }

    // Variant 3: get_if by index
    {
        std::variant<int, double, std::string> v = 3.14;
        if (auto* d = std::get_if<1>(&v)) {
            *d = 2.71;  // OK: null-checked, index 1 is double
        }
    }

    // Variant 4: Negative check - use else branch
    {
        std::variant<int, std::string> v = std::string{"test"};
        if (std::get_if<int>(&v) == nullptr) {
            // v doesn't hold int, must hold string
            auto* s = std::get_if<std::string>(&v);
            if (s) {
                s->clear();  // OK: null-checked
            }
        }
    }

    // Variant 5: get_if result checked before use
    {
        std::variant<int, std::string> v = 42;
        auto* p = std::get_if<int>(&v);
        if (p != nullptr) {
            *p = 200;  // OK: null-checked
        }
    }

    return 0;
}
