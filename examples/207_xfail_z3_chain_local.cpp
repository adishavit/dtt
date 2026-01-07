// XFAIL: Z3 chain reasoning without z3 binary — FAILS without z3 installed
//
// WHAT IS TESTED:
// Transitive arithmetic reasoning through assignment chains:
// Given: a >= 5, b = a + 5, c = b + 10
// Prove: c >= 20
//
// WHY IT'S DIFFICULT:
// 1. Requires external Z3 binary to be installed
// 2. SMT solving adds latency to compilation
// 3. Complex formulas can timeout
// 4. Z3 availability varies across platforms
//
// RATIONALE:
// Many real proofs require arithmetic reasoning:
// - Loop bounds: if i < n and we access a[i+1], need i+1 < n+1
// - Buffer math: offset + length <= capacity
// - Interval arithmetic: mid = (lo + hi) / 2 is in bounds
//
// CURRENT BEHAVIOR:
// Works correctly when z3 binary is installed and in PATH.
// Fails with "unknown" when z3 is not available.
// This test documents the Z3 dependency.
//
// NOTE: This test is a COPY of 100_z3_chain_pass.cpp to document
// that Z3 tests require external tooling.

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 20")]]
void needsTwenty(Vec& v);

int getValue();

int main() {
    Vec a;
    a.len = getValue();  // Unknown runtime value

    Vec b;
    b.len = a.len + 5;   // b.len = a.len + 5

    Vec c;
    c.len = b.len + 10;  // c.len = a.len + 15

    if (a.len >= 5) {
        // Z3 proves: a.len >= 5 AND c.len = a.len + 15 => c.len >= 20
        needsTwenty(c);  // REQUIRES Z3 to prove
    }

    return 0;
}
