// WHAT IS TESTED:
// Complex arithmetic postcondition inference with multiple operations.
// Tests: nested arithmetic, multiple arguments, compound expressions.
//
// EXPECTED: DTT tracks complex arithmetic through call sites.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    [[clang::annotate("dtt:requires=_size >= n")]]
    void require_n(int n) {}

    // Compound: (a + b) * c
    void compound_mul(int a, int b, int c) {
        _size = (a + b) * c;
    }

    // Nested: a * b + c * d
    void quad_expr(int a, int b, int c, int d) {
        _size = a * b + c * d;
    }

    // Division chain: a / b / c
    void div_chain(int a, int b, int c) {
        _size = a / b / c;
    }

    // Mixed: (a * b - c) / d + e
    void mixed_five(int a, int b, int c, int d, int e) {
        _size = (a * b - c) / d + e;
    }

    // Modulo chain: a % b % c
    void mod_chain(int a, int b, int c) {
        _size = a % b % c;
    }

    // Bitwise compound: (a & b) | c
    void bitwise_compound(int a, int b, int c) {
        _size = (a & b) | c;
    }

    // Self-referential compound: _size * factor + offset
    void scale_offset(int factor, int offset) {
        _size = _size * factor + offset;
    }

    // Power of 2 via shift: 1 << n
    void set_power2(int n) {
        _size = 1 << n;
    }

    // Align up: (size + align - 1) & ~(align - 1)
    void align_up(int align) {
        _size = (_size + align - 1) & ~(align - 1);
    }
};

// Variant 1: Compound multiplication safe
void variant1_compound_safe() {
    Vec v;
    v._size = 0;
    v.compound_mul(2, 3, 2);  // (2 + 3) * 2 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 2: Compound multiplication unsafe
void variant2_compound_unsafe() {
    Vec v;
    v._size = 0;
    v.compound_mul(1, 1, 4);  // (1 + 1) * 4 = 8
    v.at9();  // BUG: 8 <= 9
}

// Variant 3: Quad expression safe
void variant3_quad_safe() {
    Vec v;
    v._size = 0;
    v.quad_expr(2, 3, 2, 2);  // 2*3 + 2*2 = 6 + 4 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 4: Quad expression unsafe
void variant4_quad_unsafe() {
    Vec v;
    v._size = 0;
    v.quad_expr(2, 2, 2, 2);  // 2*2 + 2*2 = 4 + 4 = 8
    v.at9();  // BUG: 8 <= 9
}

// Variant 5: Division chain
void variant5_div_chain() {
    Vec v;
    v._size = 0;
    v.div_chain(100, 2, 5);  // 100 / 2 / 5 = 50 / 5 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 6: Division chain unsafe
void variant6_div_unsafe() {
    Vec v;
    v._size = 0;
    v.div_chain(100, 10, 5);  // 100 / 10 / 5 = 10 / 5 = 2
    v.at9();  // BUG: 2 <= 9
}

// Variant 7: Mixed five-arg safe
void variant7_mixed_safe() {
    Vec v;
    v._size = 0;
    v.mixed_five(10, 2, 5, 3, 5);  // (10*2 - 5) / 3 + 5 = 15/3 + 5 = 5 + 5 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 8: Mixed five-arg unsafe
void variant8_mixed_unsafe() {
    Vec v;
    v._size = 0;
    v.mixed_five(6, 2, 2, 2, 3);  // (6*2 - 2) / 2 + 3 = 10/2 + 3 = 5 + 3 = 8
    v.at9();  // BUG: 8 <= 9
}

// Variant 9: Modulo chain to zero
void variant9_mod_zero() {
    Vec v;
    v._size = 0;
    v.mod_chain(20, 7, 3);  // 20 % 7 % 3 = 6 % 3 = 0
    v.front();  // BUG: 0 <= 0
}

// Variant 10: Bitwise compound safe
void variant10_bitwise_safe() {
    Vec v;
    v._size = 0;
    v.bitwise_compound(0xFF, 0x0F, 0x06);  // (255 & 15) | 6 = 15 | 6 = 15
    v.at9();  // OK: 15 > 9
}

// Variant 11: Bitwise compound unsafe
void variant11_bitwise_unsafe() {
    Vec v;
    v._size = 0;
    v.bitwise_compound(0x0F, 0x03, 0x04);  // (15 & 3) | 4 = 3 | 4 = 7
    v.at9();  // BUG: 7 <= 9
}

// Variant 12: Scale and offset from tracked value
void variant12_scale_offset() {
    Vec v;
    v._size = 3;
    v.scale_offset(2, 5);  // 3 * 2 + 5 = 11
    v.at9();  // OK: 11 > 9
}

// Variant 13: Scale and offset unsafe
void variant13_scale_offset_unsafe() {
    Vec v;
    v._size = 2;
    v.scale_offset(2, 3);  // 2 * 2 + 3 = 7
    v.at9();  // BUG: 7 <= 9
}

// Variant 14: Power of 2 via shift
void variant14_power2() {
    Vec v;
    v._size = 0;
    v.set_power2(4);  // 1 << 4 = 16
    v.at9();  // OK: 16 > 9
}

// Variant 15: Alignment
void variant15_align_up() {
    Vec v;
    v._size = 7;
    v.align_up(4);  // (7 + 4 - 1) & ~3 = 10 & ~3 = 8
    v.at9();  // BUG: 8 <= 9
}

int main() {
    variant1_compound_safe();
    variant2_compound_unsafe();
    variant3_quad_safe();
    variant4_quad_unsafe();
    variant5_div_chain();
    variant6_div_unsafe();
    variant7_mixed_safe();
    variant8_mixed_unsafe();
    variant9_mod_zero();
    variant10_bitwise_safe();
    variant11_bitwise_unsafe();
    variant12_scale_offset();
    variant13_scale_offset_unsafe();
    variant14_power2();
    variant15_align_up();
    return 0;
}
