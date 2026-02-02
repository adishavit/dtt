// 988_fopen_patterns_pass.cpp
// Tests: Various FILE* null check patterns - all should PASS

#include <cstdio>

// Variant 1: if (fp) guard
void test_if_guard() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);
        fclose(fp);
    }
}

// Variant 2: early return on null
void test_early_return() {
    FILE* fp = fopen("file.txt", "r");
    if (!fp) return;
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
}

// Variant 3: early return with nullptr comparison
void test_early_return_nullptr() {
    FILE* fp = fopen("file.txt", "r");
    if (fp == nullptr) return;
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
}

// Variant 4: explicit nullptr != check
void test_explicit_not_null() {
    FILE* fp = fopen("file.txt", "w");
    if (fp != nullptr) {
        fputs("hello", fp);
        fclose(fp);
    }
}

// Variant 5: freopen with null check
void test_freopen_guarded() {
    FILE* fp = freopen("file.txt", "w", stdout);
    if (fp) {
        fprintf(fp, "test\n");
        // Don't close stdout
    }
}

// Variant 6: multiple file operations guarded
void test_multiple_ops_guarded() {
    FILE* fp = fopen("file.txt", "r+");
    if (fp) {
        char buf[100];
        fread(buf, 1, 50, fp);
        fseek(fp, 0, SEEK_SET);
        fwrite(buf, 1, 50, fp);
        fflush(fp);
        rewind(fp);
        int c = fgetc(fp);
        (void)c;
        fclose(fp);
    }
}

// Variant 7: nested file operations
void test_nested_files() {
    FILE* in = fopen("input.txt", "r");
    FILE* out = fopen("output.txt", "w");
    if (in && out) {
        char buf[100];
        size_t n = fread(buf, 1, sizeof(buf), in);
        fwrite(buf, 1, n, out);
        fclose(in);
        fclose(out);
    } else {
        if (in) fclose(in);
        if (out) fclose(out);
    }
}

// Variant 8: check status functions guarded
void test_status_functions() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        if (feof(fp)) {
            fclose(fp);  // Must close before early return
            return;
        }
        if (ferror(fp)) {
            clearerr(fp);
        }
        fclose(fp);
    }
}

int main() { return 0; }
