// 989_fclose_double_fail.cpp
// Tests: Double close and use-after-close - should FAIL

#include <cstdio>

// Variant 1: Double close
void test_double_close() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        fclose(fp);
        fclose(fp);  // ERROR: fp already closed (double close)
    }
}

// Variant 2: Use after close - fread
void test_use_after_close_fread() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        fclose(fp);
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // ERROR: fp already closed
    }
}

// Variant 3: Use after close - fwrite
void test_use_after_close_fwrite() {
    FILE* fp = fopen("file.txt", "w");
    if (fp) {
        fclose(fp);
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // ERROR: fp already closed
    }
}

// Variant 4: Use after close - fseek
void test_use_after_close_fseek() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        fclose(fp);
        fseek(fp, 0, SEEK_SET);  // ERROR: fp already closed
    }
}

// Variant 5: Use after close - fprintf
void test_use_after_close_fprintf() {
    FILE* fp = fopen("file.txt", "w");
    if (fp) {
        fclose(fp);
        fprintf(fp, "test\n");  // ERROR: fp already closed
    }
}

// Variant 6: Use after close - fgetc
void test_use_after_close_fgetc() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        fclose(fp);
        int c = fgetc(fp);  // ERROR: fp already closed
        (void)c;
    }
}

int main() { return 0; }
