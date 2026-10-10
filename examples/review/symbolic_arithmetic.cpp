#undef NDEBUG // Assertions are the test oracle, including in Release builds.
#include "../../src/DTTTypes.h"
#include <cassert>
#include <cstdlib>
#include <cstdint>

using dtt::SymValue;

void checkCase(int test) {
    using Binary = SymValue (*)(SymValue, SymValue);
    struct BinaryCase {
        Binary make;
        SymValue::Kind kind;
        int64_t left, right;
        std::optional<int64_t> expected;
    };
    const BinaryCase cases[] = {
        {SymValue::Add, SymValue::ADD, INT64_MAX, 1, std::nullopt},
        {SymValue::Sub, SymValue::SUB, INT64_MIN, 1, std::nullopt},
        {SymValue::Mul, SymValue::MUL, INT64_MAX, 2, std::nullopt},
        {SymValue::Div, SymValue::DIV, INT64_MIN, -1, std::nullopt},
        {SymValue::Mod, SymValue::MOD, INT64_MIN, -1, std::nullopt},
        {SymValue::Div, SymValue::DIV, 1, 0, std::nullopt},
        {SymValue::Mod, SymValue::MOD, 1, 0, std::nullopt},
        {SymValue::Shl, SymValue::SHL, INT64_MAX, 1, std::nullopt},
        {SymValue::Shl, SymValue::SHL, 1, 64, std::nullopt},
        {SymValue::Shr, SymValue::SHR, 1, -1, std::nullopt},
        {SymValue::Add, SymValue::ADD, INT64_MAX - 1, 1, INT64_MAX},
        {SymValue::Sub, SymValue::SUB, INT64_MIN + 1, 1, INT64_MIN},
        {SymValue::Mul, SymValue::MUL, INT64_MIN, 1, INT64_MIN},
        {SymValue::Div, SymValue::DIV, INT64_MIN, 1, INT64_MIN},
        {SymValue::Mod, SymValue::MOD, INT64_MIN, 1, 0},
        {SymValue::Shl, SymValue::SHL, 1, 62, int64_t{1} << 62},
        {SymValue::Shr, SymValue::SHR, INT64_MAX, 62, 1},
    };
    if (test < 34) {
        const auto& c = cases[test / 2];
        auto a = SymValue::Const(c.left), b = SymValue::Const(c.right);
        if (test % 2 == 0) {
            assert(c.make(a, b).eval() == c.expected);
        } else {
            // Bypass folding to exercise evaluation of an existing expression.
            auto expression = SymValue::Unknown();
            expression.kind = c.kind;
            expression.left = std::make_shared<SymValue>(a);
            expression.right = std::make_shared<SymValue>(b);
            assert(expression.eval() == c.expected);
        }
    } else if (test == 34) {
        assert(!SymValue::Neg(SymValue::Const(INT64_MIN)).eval());
    } else if (test == 35) {
        auto neg = SymValue::Neg(SymValue::Var("x"));
        *neg.left = SymValue::Const(INT64_MIN);
        assert(!neg.eval());
    } else if (test == 36) {
        auto sum = SymValue::Add(SymValue::Var("x"), SymValue::Const(1));
        *sum.left = SymValue::Const(INT64_MAX);
        assert(!sum.upperBound());
    } else if (test == 37) {
        auto product = SymValue::Mul(SymValue::Var("x"), SymValue::Const(2));
        *product.left = SymValue::Const(INT64_MAX);
        assert(!product.upperBound());
    } else if (test == 38) {
        auto sum = SymValue::Add(SymValue::Var("x"), SymValue::Const(1));
        *sum.left = SymValue::Const(INT64_MAX);
        assert(!sum.extractConstantOffset());
    } else if (test == 39) {
        auto difference = SymValue::Sub(SymValue::Var("x"), SymValue::Const(1));
        *difference.left = SymValue::Const(INT64_MIN);
        assert(!difference.extractConstantOffset());
    }
}

int main(int argc, char** argv) {
    if (argc == 2) checkCase(std::atoi(argv[1]));
    else for (int test = 0; test < 40; ++test) checkCase(test);
}
