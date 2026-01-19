// 840_fopen_pass.cpp
// Tests: Safe fopen/fread/fgets/fclose usage that should compile without DTT errors

#include <cstdio>
#include <cstdlib>

// =============================================================================
// SECTION 1: Guard with nullptr check before I/O
// =============================================================================

// Variant 1.1: if (fp != nullptr) guard then fread
void test_fopen_null_check() {
    FILE* fp = fopen("test.txt", "r");
    if (fp != nullptr) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: guarded
        fclose(fp);
    }
}

// Variant 1.2: if (fp) guard
void test_fopen_bool_check() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        char buf[100];
        fgets(buf, sizeof(buf), fp);  // OK: guarded
        fclose(fp);
    }
}

// Variant 1.3: early return on null
void test_fopen_early_return() {
    FILE* fp = fopen("test.txt", "r");
    if (!fp) return;
    char buf[100];
    fgets(buf, sizeof(buf), fp);  // OK: guarded
    fclose(fp);
}

// =============================================================================
// SECTION 2: fwrite with guard
// =============================================================================

// Variant 2.1: fwrite after null check
void test_fwrite_guard() {
    FILE* fp = fopen("test.txt", "w");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // OK: guarded
        fclose(fp);
    }
}

// =============================================================================
// SECTION 3: fprintf/fscanf with guard
// =============================================================================

// Variant 3.1: fprintf after null check
void test_fprintf_guard() {
    FILE* fp = fopen("test.txt", "w");
    if (fp) {
        fprintf(fp, "hello %d\n", 42);  // OK: guarded
        fclose(fp);
    }
}

// Variant 3.2: fscanf after null check
void test_fscanf_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        int x;
        fscanf(fp, "%d", &x);  // OK: guarded
        fclose(fp);
    }
}

// =============================================================================
// SECTION 4: fgetc/fputc with guard
// =============================================================================

// Variant 4.1: fgetc after null check
void test_fgetc_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        int c = fgetc(fp);  // OK: guarded
        (void)c;
        fclose(fp);
    }
}

// Variant 4.2: fputc after null check
void test_fputc_guard() {
    FILE* fp = fopen("test.txt", "w");
    if (fp) {
        fputc('A', fp);  // OK: guarded
        fclose(fp);
    }
}

// =============================================================================
// SECTION 5: Loop with fgets/feof
// =============================================================================

// Variant 5.1: while (fgets) loop
void test_while_fgets() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp) != nullptr) {
            // OK: loop condition checks result
        }
        fclose(fp);
    }
}

// Variant 5.2: check feof in loop
void test_feof_loop() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        while (!feof(fp)) {
            int c = fgetc(fp);
            if (c == EOF) break;
            (void)c;
        }
        fclose(fp);
    }
}

// =============================================================================
// SECTION 6: fseek/ftell with guard
// =============================================================================

// Variant 6.1: fseek after null check
void test_fseek_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fseek(fp, 0, SEEK_END);  // OK: guarded
        long size = ftell(fp);   // OK: guarded
        (void)size;
        fclose(fp);
    }
}

// =============================================================================
// SECTION 7: freopen with guard
// =============================================================================

// Variant 7.1: freopen and check
void test_freopen_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        fp = freopen("test2.txt", "r", fp);
        if (fp) {
            char buf[100];
            fgets(buf, sizeof(buf), fp);  // OK: double guarded
            fclose(fp);
        }
    }
}

// =============================================================================
// SECTION 8: ferror check
// =============================================================================

// Variant 8.1: check ferror after operation
void test_ferror_check() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);
        if (!ferror(fp)) {
            // OK: no error occurred
        }
        fclose(fp);
    }
}

// =============================================================================
// SECTION 9: Binary I/O with guard
// =============================================================================

// Variant 9.1: binary fread/fwrite
void test_binary_io() {
    FILE* fp = fopen("test.bin", "rb");
    if (fp) {
        char buf[100];
        size_t n = fread(buf, 1, sizeof(buf), fp);  // OK: guarded
        (void)n;
        fclose(fp);
    }
}

// =============================================================================
// SECTION 10: tmpfile with guard
// =============================================================================

// Variant 10.1: tmpfile returns FILE*
void test_tmpfile() {
    FILE* fp = tmpfile();
    if (fp) {
        fprintf(fp, "temp data\n");  // OK: guarded
        fclose(fp);
    }
}

// =============================================================================
// SECTION 11: fclose is safe on valid FILE*
// =============================================================================

// Variant 11.1: fclose after use
void test_fclose_after_use() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        // ... use file ...
        fclose(fp);  // OK: fp is valid
    }
}

int main() {
    return 0;
}
