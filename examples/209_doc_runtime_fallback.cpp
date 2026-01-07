// XFAIL: Runtime assertion fallback — DESIGN DISCUSSION, not a test
//
// WHAT IS TESTED:
// This file documents the safety concern when DTT-annotated code is
// compiled without the DTT plugin (e.g., on GCC or vanilla Clang).
//
// THE CONCERN (from our discussion):
// Q: "Can this lead to dangerous code when eg on gcc and attributes are ignored?"
// A: DTT annotations are PURE ADDITIONS of safety. Without DTT:
//    - Code compiles as if annotations don't exist
//    - Same UB potential as unannotated code
//    - NOT WORSE than code without DTT
//
// WHY RUNTIME FALLBACK IS DIFFICULT:
// 1. Annotations are metadata - no code generation
// 2. Would need macro system to generate both annotation AND assert
// 3. assert() can be disabled with NDEBUG
// 4. Performance overhead of runtime checks
//
// RATIONALE FOR WANTING THIS:
// Defense in depth - even without DTT, some checking is better than none.
// Trust but verify: CI uses DTT, but production might not.
//
// PROPOSED PATTERN (manual, not automatic):
//
//   #ifdef DTT_STATIC_CHECKS
//     #define DTT_REQUIRES(cond) [[clang::annotate("dtt:requires=" #cond)]]
//     #define DTT_ASSERT(cond)   /* static check handles it */
//   #else
//     #define DTT_REQUIRES(cond) /* no-op */
//     #define DTT_ASSERT(cond)   assert(cond)
//   #endif
//
//   DTT_REQUIRES(_size > 0)
//   void pop(Vec& v) {
//       DTT_ASSERT(v._size > 0);  // Runtime fallback
//       --v._size;
//   }

struct Vec {
    int* data;
    int _size;
};

// Without runtime fallback, this is unsafe if compiled without DTT
[[clang::annotate("dtt:requires=_size > 0")]]
int pop(Vec& v) {
    // If compiled without DTT plugin, no check happens
    // Adding assert() provides defense in depth:
    // assert(v._size > 0);  // <-- User should add this manually
    return v.data[--v._size];
}

int main() {
    Vec v;
    v._size = 0;

    // With DTT: compile error (good!)
    // Without DTT: UB at runtime (bad, but same as unannotated code)
    // With DTT + assert: compile error (good!)
    // Without DTT + assert: runtime error (better than UB!)

    // pop(v);  // Uncommenting would fail DTT check

    return 0;
}
