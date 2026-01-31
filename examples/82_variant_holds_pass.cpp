// 82_variant_holds_pass.cpp - Safe variant access with guards
// All accesses here are guarded by holds_alternative - DTT should NOT error

#include <variant>
#include <string>

int main() {
    // Variant 1: holds_alternative guard before get<T>
    {
        std::variant<int, std::string> v = 42;
        if (std::holds_alternative<int>(v)) {
            auto x = std::get<int>(v);  // OK: guarded
            (void)x;
        }
    }

    // Variant 2: holds_alternative guard before get<T> - string case
    {
        std::variant<int, std::string> v = std::string{"hello"};
        if (std::holds_alternative<std::string>(v)) {
            auto& s = std::get<std::string>(v);  // OK: guarded
            (void)s;
        }
    }

    // Variant 3: index() guard before get<I>
    {
        std::variant<int, std::string, double> v = 3.14;
        if (v.index() == 2) {
            auto d = std::get<2>(v);  // OK: guarded by index check
            (void)d;
        }
    }

    // Variant 4: Negative guard - else branch
    {
        std::variant<int, std::string> v = std::string{"world"};
        if (!std::holds_alternative<int>(v)) {
            // v must hold string (only other option)
            auto& s = std::get<std::string>(v);  // OK: logically guarded
            (void)s;
        }
    }

    // Variant 5: get_if pattern (safe null-check)
    {
        std::variant<int, std::string> v = 42;
        if (auto* p = std::get_if<int>(&v)) {
            *p = 100;  // OK: null-checked
        }
    }

    return 0;
}
