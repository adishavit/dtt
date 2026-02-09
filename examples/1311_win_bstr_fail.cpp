// 1311_win_bstr_fail.cpp
// Tests: Unsafe BSTR and COM allocator usage that DTT should catch
// Mirrors patterns from 839_malloc_fail.cpp

#include "win_api.h"

// =============================================================================
// SECTION 1: BSTR leak (scope-exit violation)
// =============================================================================

// Variant 1.1: SysAllocString without SysFreeString
void test_bstr_leak() {
    BSTR b = SysAllocString(L"leaked");
    // ERROR: @allocated == 1 at scope exit, need @allocated == 0
}

// Variant 1.2: SysAllocStringLen without free
void test_bstr_len_leak() {
    BSTR b = SysAllocStringLen(L"leak", 4);
    // ERROR: @allocated == 1 at scope exit
}

// Variant 1.3: SysAllocStringByteLen without free
void test_bstr_byte_len_leak() {
    BSTR b = SysAllocStringByteLen("leak", 4);
    // ERROR: @allocated == 1 at scope exit
}

// =============================================================================
// SECTION 2: CoTaskMem leak
// =============================================================================

// Variant 2.1: CoTaskMemAlloc without CoTaskMemFree
void test_cotaskmem_leak() {
    void* p = CoTaskMemAlloc(100);
    // ERROR: @allocated == 1 at scope exit
}

// =============================================================================
// SECTION 3: LocalAlloc leak
// =============================================================================

// Variant 3.1: LocalAlloc without LocalFree
void test_local_leak() {
    void* p = LocalAlloc(0, 100);
    // ERROR: @allocated == 1 at scope exit
}

// =============================================================================
// SECTION 4: GlobalAlloc leak
// =============================================================================

// Variant 4.1: GlobalAlloc without GlobalFree
void test_global_leak() {
    void* p = GlobalAlloc(0, 100);
    // ERROR: @allocated == 1 at scope exit
}

int main() { return 0; }
