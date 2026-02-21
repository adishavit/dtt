// 1700: Inline asm output operands - DTT correctly handles these
// GCC-style inline asm "=r"(var) output constraints initialize the variable.
// DTT models asm output operands, so it correctly recognizes them as initialized.
// Found by scanning libwebp (src/dsp/cpu.c:74).

#include <stdint.h>

// Variant 1: single output operand
int variant1_single_output() {
    int result;
    __asm__ volatile("movl $42, %0" : "=r"(result));
    return result;  // OK: result initialized by asm output operand
}

// Variant 2: multiple output operands (libwebp xgetbv pattern)
uint64_t variant2_multiple_outputs() {
    const uint32_t ecx = 0;
    uint32_t eax, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=d"(edx)
                     : "c"(ecx));
    return ((uint64_t)edx << 32) | eax;  // OK: both set by asm
}

// Variant 3: output operand used in arithmetic
int variant3_output_in_expr() {
    int a, b;
    __asm__ volatile("movl $10, %0\n\tmovl $20, %1"
                     : "=r"(a), "=r"(b));
    return a + b;  // OK: a and b initialized by asm
}

// Variant 4: read-write operand ("+r" constraint)
int variant4_readwrite_operand() {
    int val = 5;
    __asm__ volatile("addl $1, %0" : "+r"(val));
    return val;  // OK: val both read and written by asm
}

int main() {
    return variant1_single_output()
         + (int)variant2_multiple_outputs()
         + variant3_output_in_expr()
         + variant4_readwrite_operand();
}
