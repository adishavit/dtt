// 990_fopen_mode_pass.cpp
// Tests: FILE* mode tracking - correct read/write usage should PASS

#include <cstdio>

// Variant 1: Read mode - fread OK
void test_read_mode_fread() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: opened for reading
        fclose(fp);
    }
}

// Variant 2: Write mode - fwrite OK
void test_write_mode_fwrite() {
    FILE* fp = fopen("file.txt", "w");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // OK: opened for writing
        fclose(fp);
    }
}

// Variant 3: Read-write mode (r+) - both OK
void test_rplus_mode() {
    FILE* fp = fopen("file.txt", "r+");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: r+ allows read
        fwrite(buf, 1, 10, fp);          // OK: r+ allows write
        fclose(fp);
    }
}

// Variant 4: Write-read mode (w+) - both OK
void test_wplus_mode() {
    FILE* fp = fopen("file.txt", "w+");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);          // OK: w+ allows write
        rewind(fp);
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: w+ allows read
        fclose(fp);
    }
}

// Variant 5: Append mode (a) - fwrite OK
void test_append_mode() {
    FILE* fp = fopen("file.txt", "a");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // OK: append allows write
        fclose(fp);
    }
}

// Variant 6: Append-read mode (a+) - both OK
void test_aplus_mode() {
    FILE* fp = fopen("file.txt", "a+");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: a+ allows read
        fwrite(buf, 1, 10, fp);          // OK: a+ allows write
        fclose(fp);
    }
}

// Variant 7: Binary read mode (rb)
void test_binary_read() {
    FILE* fp = fopen("file.bin", "rb");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // OK: rb allows read
        fclose(fp);
    }
}

// Variant 8: Binary write mode (wb)
void test_binary_write() {
    FILE* fp = fopen("file.bin", "wb");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // OK: wb allows write
        fclose(fp);
    }
}

int main() { return 0; }
