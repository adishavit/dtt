// 80_variant_index_pass.cpp - Safe std::variant access patterns
// All accesses here are correct - DTT should NOT error

#include <variant>
#include <string>
#include <vector>

int main() {
    // Variant 1: Access immediately after assignment (same type)
    {
        std::variant<int, std::string> v = 42;
        auto x = std::get<int>(v);  // OK: v holds int (index 0)
        (void)x;
    }

    // Variant 2: Access by index after assignment
    {
        std::variant<int, std::string, double> v;
        v = std::string{"hello"};
        auto& s = std::get<1>(v);  // OK: v holds string (index 1)
        (void)s;
    }

    // Variant 3: Access after construction with specific type
    {
        std::variant<int, std::string> v{std::string{"world"}};
        auto s = std::get<std::string>(v);  // OK: constructed with string
        (void)s;
    }

    // Variant 4: Multiple reassignments, access after last assignment
    {
        std::variant<int, double, std::string> v = 42;
        v = 3.14;
        v = std::string{"final"};
        auto& s = std::get<std::string>(v);  // OK: last assignment was string
        (void)s;
    }

    // Variant 5: Default construction gives index 0
    {
        std::variant<int, std::string> v;  // Default: holds int{} (index 0)
        auto x = std::get<0>(v);  // OK: default is first alternative
        (void)x;
    }

    return 0;
}
