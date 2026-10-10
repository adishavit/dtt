// 987_fopen_null_check_fail.cpp
// Tests: fopen with MISSING null check - should FAIL
// fopen can return NULL on failure, so fread/fwrite/fclose without check is unsafe

#include <cstdio>

// Variant 1: fread without null check
void test_fread_unguarded() {
    FILE* fp = fopen("file.txt", "r");
    char buf[100];
    fread(buf, 1, sizeof(buf), fp);  // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
    fclose(fp);                       // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
}

// Variant 2: fwrite without null check
void test_fwrite_unguarded() {
    FILE* fp = fopen("file.txt", "w");
    const char* data = "hello";
    fwrite(data, 1, 5, fp);  // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
    fclose(fp);              // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
}

// Variant 3: fgets without null check
void test_fgets_unguarded() {
    FILE* fp = fopen("file.txt", "r");
    char buf[100];
    fgets(buf, sizeof(buf), fp);  // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
    fclose(fp);                   // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
}

// Variant 4: fputs without null check
void test_fputs_unguarded() {
    FILE* fp = fopen("file.txt", "w");
    fputs("hello", fp);  // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
    fclose(fp);          // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
}

// Variant 5: fprintf without null check
void test_fprintf_unguarded() {
    FILE* fp = fopen("file.txt", "w");
    fprintf(fp, "value: %d\n", 42);  // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
    fclose(fp);                      // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
}

// Variant 6: fscanf without null check
void test_fscanf_unguarded() {
    FILE* fp = fopen("file.txt", "r");
    int x;
    fscanf(fp, "%d", &x);  // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
    fclose(fp);            // ERROR: fp may be null  dtt-error {{_nonnull != 0}}
}

int main() { return 0; }
