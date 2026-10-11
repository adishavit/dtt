// C11 aligned_alloc contracts; unavailable in the Windows UCRT.
#include <cstdlib>

// =============================================================================
// SECTION 7: aligned_alloc with guard
// =============================================================================

// Variant 7.1: aligned_alloc with null check
void test_aligned_alloc_guard() {
    void* p = aligned_alloc(16, 64);
    if (p) {
        // OK: guarded
        free(p);
    }
}

int main() { return 0; }
