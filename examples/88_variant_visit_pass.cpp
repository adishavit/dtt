// 88_variant_visit_pass.cpp - std::visit is always type-safe
// std::visit automatically dispatches to correct alternative - DTT should NOT error

#include <variant>
#include <string>

int main() {
    // Variant 1: Basic visit with lambda
    {
        std::variant<int, std::string> v = 42;
        std::visit([](auto& x) {
            // x is always the correct type - no type error possible
            (void)x;
        }, v);
    }

    // Variant 2: Visit with explicit visitor struct
    {
        struct Visitor {
            void operator()(int i) { (void)i; }
            void operator()(const std::string& s) { (void)s; }
        };

        std::variant<int, std::string> v = std::string{"hello"};
        std::visit(Visitor{}, v);  // OK: type-safe dispatch
    }

    // Variant 3: Visit returning value
    {
        std::variant<int, double> v = 3.14;
        auto result = std::visit([](auto x) -> double {
            return static_cast<double>(x);
        }, v);
        (void)result;
    }

    // Variant 4: Visit with multiple statements
    {
        std::variant<int, std::string, double> v = std::string{"test"};
        std::visit([](auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::string>) {
                x.append("!");  // OK: only executed when x is string
            }
        }, v);
    }

    return 0;
}
