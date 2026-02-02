// 841_fopen_fail.cpp
// Tests: Unsafe fopen/fread/fgets/fclose usage (DTT catches all variants)

#include <cstdio>
#include <cstdlib>

// =============================================================================
// SECTION 1: I/O without null check
// =============================================================================

// Variant 1.1: fread without checking fopen result
void test_fread_no_guard() {
    FILE* fp = fopen("nonexistent.txt", "r");
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);  // ERROR: fp might be null
    if (fp) fclose(fp);
}

// Variant 1.2: fwrite without checking fopen result
void test_fwrite_no_guard() {
    FILE* fp = fopen("/invalid/path.txt", "w");
    const char* data = "hello";
    fwrite(data, 1, 5, fp);  // ERROR: fp might be null
    if (fp) fclose(fp);
}

// Variant 1.3: fgets without checking fopen result
void test_fgets_no_guard() {
    FILE* fp = fopen("nonexistent.txt", "r");
    char buf[100];
    fgets(buf, sizeof(buf), fp);  // ERROR: fp might be null
    if (fp) fclose(fp);
}

// =============================================================================
// SECTION 2: I/O on parameter FILE*
// =============================================================================

// Variant 2.1: fread on parameter
void test_fread_param(FILE* fp) {
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);  // ERROR: can't prove fp is valid
}

// Variant 2.2: fprintf on parameter
void test_fprintf_param(FILE* fp) {
    fprintf(fp, "hello\n");  // ERROR: can't prove fp is valid
}

// =============================================================================
// SECTION 3: I/O after fclose
// =============================================================================

// Variant 3.1: fread after fclose
void test_fread_after_fclose() {
    FILE* fp = fopen("test.txt", "r");
    if (!fp) return;
    fclose(fp);
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);  // ERROR: fp was closed
}

// Variant 3.2: fprintf after fclose
void test_fprintf_after_fclose() {
    FILE* fp = fopen("test.txt", "w");
    if (!fp) return;
    fclose(fp);
    fprintf(fp, "hello\n");  // ERROR: fp was closed
}

// =============================================================================
// SECTION 4: I/O in wrong branch
// =============================================================================

// Variant 4.1: I/O in else of null check
void test_wrong_branch() {
    FILE* fp = fopen("test.txt", "r");
    if (fp) {
        // OK here
        fclose(fp);
    } else {
        char buf[100];
        fgets(buf, sizeof(buf), fp);  // ERROR: fp is null in else
    }
}

// =============================================================================
// SECTION 5: Double fclose
// =============================================================================

// Variant 5.1: double fclose
void test_double_fclose() {
    FILE* fp = fopen("test.txt", "r");
    if (!fp) return;
    fclose(fp);
    fclose(fp);  // ERROR: double fclose
}

// =============================================================================
// SECTION 6: fclose on null
// =============================================================================

// Variant 6.1: fclose(NULL) is undefined
void test_fclose_null() {
    FILE* fp = nullptr;
    fclose(fp);  // ERROR: fclose(NULL) is undefined (unlike free)
}

// Variant 6.2: fclose on potentially null
void test_fclose_maybe_null() {
    FILE* fp = fopen("maybe.txt", "r");
    fclose(fp);  // ERROR: fp might be null
}

// =============================================================================
// SECTION 7: Stale FILE* check
// =============================================================================

// Variant 7.1: stale validity check
void test_stale_check() {
    FILE* fp = fopen("test.txt", "r");
    bool was_valid = (fp != nullptr);
    if (fp) fclose(fp);
    if (was_valid) {
        char buf[100];
        fgets(buf, sizeof(buf), fp);  // ERROR: stale check, fp was closed
    }
}

// =============================================================================
// SECTION 8: ferror doesn't guarantee validity
// =============================================================================

// Variant 8.1: ferror on potentially null
void test_ferror_no_guard() {
    FILE* fp = fopen("test.txt", "r");
    if (!ferror(fp)) {  // ERROR: fp might be null first
        // ...
    }
    if (fp) fclose(fp);
}

// =============================================================================
// SECTION 9: fseek/ftell without guard
// =============================================================================

// Variant 9.1: fseek on potentially null
void test_fseek_no_guard() {
    FILE* fp = fopen("test.txt", "r");
    fseek(fp, 0, SEEK_END);  // ERROR: fp might be null
    if (fp) fclose(fp);
}

// Variant 9.2: ftell on potentially null
void test_ftell_no_guard() {
    FILE* fp = fopen("test.txt", "r");
    long pos = ftell(fp);  // ERROR: fp might be null
    (void)pos;
    if (fp) fclose(fp);
}

// =============================================================================
// SECTION 10: Wrong FILE* checked
// =============================================================================

// Variant 10.1: check fp1, use fp2
void test_wrong_file_checked() {
    FILE* fp1 = fopen("test1.txt", "r");
    FILE* fp2 = fopen("test2.txt", "r");
    if (fp1) {
        char buf[100];
        fgets(buf, sizeof(buf), fp2);  // ERROR: checked fp1, not fp2
        fclose(fp1);
    }
    if (fp2) fclose(fp2);
}

// =============================================================================
// SECTION 11: Use after freopen failure
// =============================================================================

// Variant 11.1: freopen might return null
void test_freopen_no_check() {
    FILE* fp = fopen("test.txt", "r");
    if (!fp) return;
    fp = freopen("nonexistent.txt", "r", fp);  // Might fail
    char buf[100];
    fgets(buf, sizeof(buf), fp);  // ERROR: fp might be null
    if (fp) fclose(fp);
}

int main() {
    return 0;
}
