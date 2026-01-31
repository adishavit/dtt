// 1000_union_cve_discriminant_fail.cpp - Missing discriminant check patterns
// Based on CVE-2022-41033 (COM+ Event System), COMFUSION research, CWE-843
// Pattern: Reading union member without checking discriminant first

// Simulates COM VARIANT-style tagged union
enum VariantType { VT_INT = 0, VT_BSTR = 8, VT_DISPATCH = 9, VT_UNKNOWN = 13 };
union VariantData {
    int intVal;
    char* bstrVal;  // String pointer
    void* punkVal;  // IUnknown pointer
};
struct Variant {
    VariantType vt;
    VariantData data;
};

// Variant 1: Direct access without discriminant check (CVE-2022-41033 pattern)
void test_no_discriminant_check(Variant* var) {
    // Attacker controls var->vt but code assumes VT_BSTR
    char* str = var->data.bstrVal;  // FAIL: no check that vt == VT_BSTR
    (void)str;
}

// Variant 2: Wrong discriminant assumption
void test_wrong_type_assumption(Variant* var) {
    if (var->vt != VT_INT) {
        // Assumes if not int, must be string - WRONG!
        char* str = var->data.bstrVal;  // FAIL: could be VT_DISPATCH or VT_UNKNOWN
        (void)str;
    }
}

// Variant 3: Partial check - missing case
void test_partial_check(Variant* var) {
    if (var->vt == VT_INT) {
        int x = var->data.intVal;  // OK
        (void)x;
    }
    // Falls through without else - code continues assuming bstrVal
    char* str = var->data.bstrVal;  // FAIL: vt might not be VT_BSTR
    (void)str;
}

// PROPVARIANT-style union (based on Windows PROPVARIANT)
struct PropVariant {
    unsigned short vt;
    union {
        int intVal;
        float fltVal;
        struct { unsigned int count; int* pElems; } caInt;
        struct { unsigned int cbSize; char* pBlobData; } blob;
    };
};

// Variant 4: PROPVARIANT array/blob confusion (CVE-2022-41033 exact pattern)
void test_propvariant_confusion(PropVariant* pv) {
    // Code expects VT_VECTOR|VT_I4 but doesn't check
    // Attacker sends VT_BLOB which aliases pElems to pBlobData
    int* arr = pv->caInt.pElems;  // FAIL: could be blob data misinterpreted as pointer
    int x = arr[0];  // Dereference attacker-controlled "pointer"
    (void)x;
}

// Variant 5: Size mismatch due to type confusion
void test_size_mismatch(PropVariant* pv) {
    // Assumes integer array
    unsigned int count = pv->caInt.count;  // FAIL: if blob, this is cbSize (byte count)
    for (unsigned i = 0; i < count; i++) {
        int* arr = pv->caInt.pElems;
        (void)arr[i];  // May read out of bounds if blob
    }
}

int main() {
    Variant v;
    v.vt = VT_INT;
    v.data.intVal = 42;
    test_no_discriminant_check(&v);
    test_wrong_type_assumption(&v);
    test_partial_check(&v);

    PropVariant pv;
    pv.vt = 0;
    pv.intVal = 0;
    test_propvariant_confusion(&pv);
    test_size_mismatch(&pv);
    return 0;
}
