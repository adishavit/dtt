// C11 aligned_alloc contracts; unavailable in the Windows UCRT.
#include <cstdlib>

// ============================================================================
// ALIGNED ALLOCATION MISMATCHES
// ============================================================================

// Variant 23: aligned_alloc with delete
void variant23_aligned_alloc_delete() {
    int* p = (int*)aligned_alloc(64, sizeof(int) * 16);
    delete p;  // BUG: aligned_alloc should use free()  dtt-error {{mismatched deallocation}}
}

// Variant 24: aligned_alloc with delete[]
void variant24_aligned_alloc_delete_array() {
    int* arr = (int*)aligned_alloc(64, sizeof(int) * 16);
    delete[] arr;  // BUG: aligned_alloc should use free()  dtt-error {{mismatched deallocation}}
}

int main() { return 0; }
