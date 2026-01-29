// 90_any_type_pass.cpp - Safe std::any_cast patterns
// All casts here are type-safe - DTT should NOT error

#include <any>
#include <string>

int main() {
    // Any 1: Cast to correct type immediately after assignment
    {
        std::any a = 42;
        auto x = std::any_cast<int>(a);  // OK: a holds int
        (void)x;
    }

    // Any 2: Cast to string after string assignment
    {
        std::any a = std::string{"hello"};
        auto s = std::any_cast<std::string>(a);  // OK: a holds string
        (void)s;
    }

    // Any 3: Pointer cast with null check
    {
        std::any a = 3.14;
        if (auto* p = std::any_cast<double>(&a)) {
            *p = 2.71;  // OK: null-checked
        }
    }

    // Any 4: Type check with typeid before cast
    {
        std::any a = 42;
        if (a.type() == typeid(int)) {
            auto x = std::any_cast<int>(a);  // OK: type verified
            (void)x;
        }
    }

    // Any 5: Reassignment then correct cast
    {
        std::any a = 42;
        a = std::string{"world"};
        auto s = std::any_cast<std::string>(a);  // OK: a now holds string
        (void)s;
    }

    // Any 6: make_any then cast
    {
        auto a = std::make_any<double>(3.14);
        auto d = std::any_cast<double>(a);  // OK: make_any<double>
        (void)d;
    }

    // Any 7: has_value check before cast (only checks non-empty, not type)
    {
        std::any a = 42;
        if (a.has_value()) {
            // Note: has_value() doesn't verify type, so this could still throw
            // But if we know the type from context, it's safe
            auto x = std::any_cast<int>(a);  // OK in this context
            (void)x;
        }
    }

    return 0;
}
