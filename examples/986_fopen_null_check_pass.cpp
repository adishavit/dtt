// 986_fopen_null_check_pass.cpp
// Tests: fopen with proper null check - should PASS

#include <cstdio>

// Variant 1: if (fp) guard
void test_fread_guarded() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: guarded
        fclose(fp);
    }
}

// Variant 2: early return on null
void test_fread_early_return() {
    FILE* fp = fopen("file.txt", "r");
    if (!fp) return;
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);  // OK: early return guards
    fclose(fp);
}

// Variant 3: fwrite guarded
void test_fwrite_guarded() {
    FILE* fp = fopen("file.txt", "w");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // OK: guarded
        fclose(fp);
    }
}

int main() { return 0; }
