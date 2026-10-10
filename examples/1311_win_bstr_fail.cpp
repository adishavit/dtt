// 1311: BSTR/COM allocator leak detection for unchecked results
// With `when` guards, ghost fields are only set when _nonnull is proven.
// These unchecked results have unknown _nonnull, so @allocated is never
// set — leaks go undetected. Requires deferred guard evaluation.

#include "win_api.h"

// =============================================================================
// SECTION 1: BSTR leak (scope-exit violation)
// =============================================================================

// Variant 1.1: SysAllocString without SysFreeString
void test_bstr_leak() {
    BSTR b = SysAllocString(L"leaked");  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: @allocated == 1 at scope exit, need @allocated == 0
}

// Variant 1.2: SysAllocStringLen without free
void test_bstr_len_leak() {
    BSTR b = SysAllocStringLen(L"leak", 4);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: @allocated == 1 at scope exit
}

// Variant 1.3: SysAllocStringByteLen without free
void test_bstr_byte_len_leak() {
    BSTR b = SysAllocStringByteLen("leak", 4);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: @allocated == 1 at scope exit
}

// =============================================================================
// SECTION 2: CoTaskMem leak
// =============================================================================

// Variant 2.1: CoTaskMemAlloc without CoTaskMemFree
void test_cotaskmem_leak() {
    void* p = CoTaskMemAlloc(100);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: @allocated == 1 at scope exit
}

// =============================================================================
// SECTION 3: LocalAlloc leak
// =============================================================================

// Variant 3.1: LocalAlloc without LocalFree
void test_local_leak() {
    void* p = LocalAlloc(0, 100);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: @allocated == 1 at scope exit
}

// =============================================================================
// SECTION 4: GlobalAlloc leak
// =============================================================================

// Variant 4.1: GlobalAlloc without GlobalFree
void test_global_leak() {
    void* p = GlobalAlloc(0, 100);  // dtt-error {{resource pointer that is never null-checked}}
    // ERROR: @allocated == 1 at scope exit
}

int main() { return 0; }
