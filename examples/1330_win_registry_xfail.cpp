// 1330_win_registry_xfail.cpp
// Tests: Registry HANDLE leak that DTT SHOULD catch but can't yet
//
// Known gap: RegOpenKeyExW writes the HANDLE to an out-param (HKEY*),
// but FreeFunctionPostcondition sets ghost fields on the argument variable
// (the pointer), not the pointed-to value. So (&hKey).@regkey != hKey.@regkey.

#include "win_api.h"

// =============================================================================
// SECTION 1: RegOpenKeyExW without RegCloseKey
// =============================================================================

// Variant 1.1: Registry key leak (no RegCloseKey)
void test_registry_leak() {
    HKEY hKey;
    RegOpenKeyExW(nullptr, L"Software", 0, 0, &hKey);
    // GAP: hKey holds an open registry key but DTT cannot track it
    // because the resource was written to an out-param, not returned
}

// Variant 1.2: Conditional RegCloseKey (one path leaks)
void test_registry_conditional_leak(bool cond) {
    HKEY hKey;
    RegOpenKeyExW(nullptr, L"Software", 0, 0, &hKey);
    if (cond) {
        RegCloseKey(hKey);
    }
    // GAP: if !cond, registry key leaks
}

int main() { return 0; }
