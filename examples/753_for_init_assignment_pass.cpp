// Tests: For-loop initializer assignment should track non-null
// Issue #53: for (p = val; *p; p++) should know p is assigned from val
// Gap: DTT warns about null even though p is assigned in for-init

// Variant 1: Basic for-init assignment from parameter
void variant1_for_init_from_param(const char* str) {
    if (!str) return;  // str is non-null after this

    for (const char* p = str; *p; p++) {
        // p starts as str (non-null), should not warn
        char c = *p;  // gap: may warn about null
        (void)c;
    }
}

// Variant 2: For-init assignment from known non-null
void variant2_for_init_known_nonnull() {
    const char* str = "hello";  // string literal, always non-null

    for (const char* p = str; *p != '\0'; p++) {
        char c = *p;  // gap: may warn about null
        (void)c;
    }
}

// Variant 3: For-init with array (always non-null)
void variant3_for_init_array() {
    int arr[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};

    for (int* p = arr; *p != 0; p++) {
        *p = *p + 1;  // gap: may warn about null
    }
}

// Variant 4: Nested for-init assignments
void variant4_nested_for_init(const char* s1, const char* s2) {
    if (!s1 || !s2) return;

    for (const char* p1 = s1; *p1; p1++) {
        for (const char* p2 = s2; *p2; p2++) {
            if (*p1 == *p2) return;  // gap: may warn
        }
    }
}

int main() {
    variant1_for_init_from_param("test");
    variant2_for_init_known_nonnull();
    variant3_for_init_array();
    variant4_nested_for_init("abc", "xyz");
    return 0;
}
