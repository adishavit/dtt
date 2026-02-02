// WHAT IS TESTED:
// Stress test for arithmetic postcondition inference.
// Tests: edge cases, overflow-like patterns, complex combinations.
//
// EXPECTED: DTT correctly handles arithmetic edge cases.

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }

    [[clang::annotate("dtt:requires=_size == n")]]
    void require_eq(int n) {}

    // Large constant operations
    void set_large(int a) {
        _size = a * 1000 + 500;
    }

    // Negative intermediate
    void set_with_negative(int a, int b) {
        _size = a - b + 20;
    }

    // Divide then multiply (lossy)
    void div_mul(int a) {
        _size = (a / 4) * 4;  // Rounds down to multiple of 4
    }

    // Nested operations
    void deep_nest(int a, int b, int c) {
        _size = ((a + b) * c - a) / 2;
    }

    // Modulo patterns
    void mod_arith(int a, int b) {
        _size = (a % 10) + (b % 10);  // Max is 18
    }

    // Bitwise mask patterns
    void bit_patterns(int a) {
        _size = (a & 0xF) | 0x10;  // Low nibble + 16, range 16-31
    }

    // Shift patterns
    void shift_combo(int a) {
        _size = (a << 2) - (a >> 1);  // a*4 - a/2 ≈ 3.5a
    }

    // XOR pattern
    void xor_magic(int a) {
        _size = (a ^ a) + 10;  // Always 10
    }

    // Self-referential complex
    void self_complex() {
        _size = (_size * 3 + _size) / 2;  // = 2 * _size
    }

    // Multiple old refs
    void multi_old(int delta) {
        _size = _size + delta - (_size / 2);  // _size/2 + delta
    }
};

// Variant 1: Large constant
void variant1_large() {
    Vec v;
    v._size = 0;
    v.set_large(1);  // 1 * 1000 + 500 = 1500
    v.at9();  // OK: 1500 > 9
}

// Variant 2: Large constant edge
void variant2_large_zero() {
    Vec v;
    v._size = 5;
    v.set_large(0);  // 0 * 1000 + 500 = 500
    v.require_eq(500);  // OK
}

// Variant 3: Negative intermediate
void variant3_negative_intermediate() {
    Vec v;
    v._size = 0;
    v.set_with_negative(5, 10);  // 5 - 10 + 20 = 15
    v.at9();  // OK: 15 > 9
}

// Variant 4: Negative intermediate unsafe
void variant4_negative_result() {
    Vec v;
    v._size = 0;
    v.set_with_negative(5, 30);  // 5 - 30 + 20 = -5
    v.front();  // BUG: -5 <= 0
}

// Variant 5: Division loss safe
void variant5_div_loss_safe() {
    Vec v;
    v._size = 0;
    v.div_mul(43);  // (43/4)*4 = 10*4 = 40
    v.at9();  // OK: 40 > 9
}

// Variant 6: Division loss unsafe
void variant6_div_loss_unsafe() {
    Vec v;
    v._size = 0;
    v.div_mul(35);  // (35/4)*4 = 8*4 = 32
    v.require_eq(35);  // BUG: 32 != 35
}

// Variant 7: Deep nesting
void variant7_deep_nest() {
    Vec v;
    v._size = 0;
    v.deep_nest(4, 6, 3);  // ((4+6)*3-4)/2 = (30-4)/2 = 26/2 = 13
    v.at9();  // OK: 13 > 9
}

// Variant 8: Deep nesting unsafe
void variant8_deep_nest_unsafe() {
    Vec v;
    v._size = 0;
    v.deep_nest(2, 2, 2);  // ((2+2)*2-2)/2 = (8-2)/2 = 6/2 = 3
    v.at9();  // BUG: 3 <= 9
}

// Variant 9: Modulo arithmetic
void variant9_mod_arith() {
    Vec v;
    v._size = 0;
    v.mod_arith(17, 15);  // (17%10)+(15%10) = 7+5 = 12
    v.at9();  // OK: 12 > 9
}

// Variant 10: Modulo arithmetic unsafe
void variant10_mod_unsafe() {
    Vec v;
    v._size = 0;
    v.mod_arith(10, 10);  // (10%10)+(10%10) = 0+0 = 0
    v.front();  // BUG: 0 <= 0
}

// Variant 11: Bit patterns
void variant11_bit_pattern() {
    Vec v;
    v._size = 0;
    v.bit_patterns(0xFF);  // (255&15)|16 = 15|16 = 31
    v.at9();  // OK: 31 > 9
}

// Variant 12: Bit patterns minimal
void variant12_bit_minimal() {
    Vec v;
    v._size = 0;
    v.bit_patterns(0);  // (0&15)|16 = 0|16 = 16
    v.at9();  // OK: 16 > 9
}

// Variant 13: Shift combo
void variant13_shift() {
    Vec v;
    v._size = 0;
    v.shift_combo(4);  // 4*4 - 4/2 = 16 - 2 = 14
    v.at9();  // OK: 14 > 9
}

// Variant 14: Shift combo unsafe
void variant14_shift_unsafe() {
    Vec v;
    v._size = 0;
    v.shift_combo(2);  // 2*4 - 2/2 = 8 - 1 = 7
    v.at9();  // BUG: 7 <= 9
}

// Variant 15: XOR self-cancel
void variant15_xor() {
    Vec v;
    v._size = 0;
    v.xor_magic(12345);  // (12345^12345)+10 = 0+10 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 16: Self-referential
void variant16_self() {
    Vec v;
    v._size = 6;
    v.self_complex();  // (6*3+6)/2 = 24/2 = 12
    v.at9();  // OK: 12 > 9
}

// Variant 17: Self-referential unsafe
void variant17_self_unsafe() {
    Vec v;
    v._size = 4;
    v.self_complex();  // (4*3+4)/2 = 16/2 = 8
    v.at9();  // BUG: 8 <= 9
}

// Variant 18: Multi old
void variant18_multi_old() {
    Vec v;
    v._size = 10;
    v.multi_old(5);  // 10 + 5 - 5 = 10
    v.at9();  // OK: 10 > 9
}

// Variant 19: Multi old unsafe
void variant19_multi_old_unsafe() {
    Vec v;
    v._size = 8;
    v.multi_old(1);  // 8 + 1 - 4 = 5
    v.at9();  // BUG: 5 <= 9
}

int main() {
    variant1_large();
    variant2_large_zero();
    variant3_negative_intermediate();
    variant4_negative_result();
    variant5_div_loss_safe();
    variant6_div_loss_unsafe();
    variant7_deep_nest();
    variant8_deep_nest_unsafe();
    variant9_mod_arith();
    variant10_mod_unsafe();
    variant11_bit_pattern();
    variant12_bit_minimal();
    variant13_shift();
    variant14_shift_unsafe();
    variant15_xor();
    variant16_self();
    variant17_self_unsafe();
    variant18_multi_old();
    variant19_multi_old_unsafe();
    return 0;
}
