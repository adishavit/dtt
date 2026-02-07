// 1310_win_bstr_pass.cpp
// Tests: Safe BSTR and COM allocator usage that should compile without DTT errors
// Mirrors pattern from 838_malloc_pass.cpp

#include "win_api.h"

// =============================================================================
// SECTION 1: SysAllocString + null check + SysFreeString
// =============================================================================

// Variant 1.1: if (b) guard then free
void test_bstr_null_check() {
    BSTR b = SysAllocString(L"hello");
    if (b) {
        SysFreeString(b);
    }
}

// Variant 1.2: early return on null
void test_bstr_early_return() {
    BSTR b = SysAllocString(L"world");
    if (!b) return;
    SysFreeString(b);
}

// Variant 1.3: SysAllocStringLen + guard + free
void test_bstr_alloc_len() {
    BSTR b = SysAllocStringLen(L"test", 4);
    if (b) {
        SysFreeString(b);
    }
}

// =============================================================================
// SECTION 2: CoTaskMemAlloc + null check + CoTaskMemFree
// =============================================================================

// Variant 2.1: if (p) guard then free
void test_cotaskmem_null_check() {
    void* p = CoTaskMemAlloc(100);
    if (p) {
        CoTaskMemFree(p);
    }
}

// Variant 2.2: early return on null
void test_cotaskmem_early_return() {
    void* p = CoTaskMemAlloc(200);
    if (!p) return;
    CoTaskMemFree(p);
}

// =============================================================================
// SECTION 3: LocalAlloc + null check + LocalFree
// =============================================================================

// Variant 3.1: if (p) guard then free
void test_local_null_check() {
    void* p = LocalAlloc(0, 100);
    if (p) {
        LocalFree(p);
    }
}

// Variant 3.2: early return on null
void test_local_early_return() {
    void* p = LocalAlloc(0, 200);
    if (!p) return;
    LocalFree(p);
}

// =============================================================================
// SECTION 4: GlobalAlloc + null check + GlobalFree
// =============================================================================

// Variant 4.1: if (p) guard then free
void test_global_null_check() {
    void* p = GlobalAlloc(0, 100);
    if (p) {
        GlobalFree(p);
    }
}

int main() {
    return 0;
}
