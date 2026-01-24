// 991_fopen_mode_fail.cpp
// Tests: FILE* mode tracking - incorrect read/write usage should FAIL

#include <cstdio>

// Variant 1: Read mode - fwrite NOT OK
void test_read_mode_fwrite() {
    FILE* fp = fopen("file.txt", "r");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // ERROR: opened read-only, cannot write
        fclose(fp);
    }
}

// Variant 2: Write mode - fread NOT OK
void test_write_mode_fread() {
    FILE* fp = fopen("file.txt", "w");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // ERROR: opened write-only, cannot read
        fclose(fp);
    }
}

// Variant 3: Append mode - fread NOT OK
void test_append_mode_fread() {
    FILE* fp = fopen("file.txt", "a");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // ERROR: append is write-only, cannot read
        fclose(fp);
    }
}

// Variant 4: Binary read mode - fwrite NOT OK
void test_binary_read_fwrite() {
    FILE* fp = fopen("file.bin", "rb");
    if (fp) {
        const char* data = "hello";
        fwrite(data, 1, 5, fp);  // ERROR: rb is read-only, cannot write
        fclose(fp);
    }
}

// Variant 5: Binary write mode - fread NOT OK
void test_binary_write_fread() {
    FILE* fp = fopen("file.bin", "wb");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // ERROR: wb is write-only, cannot read
        fclose(fp);
    }
}

// Variant 6: Binary append mode - fread NOT OK
void test_binary_append_fread() {
    FILE* fp = fopen("file.bin", "ab");
    if (fp) {
        char buf[100];
        fread(buf, 1, sizeof(buf), fp);  // ERROR: ab is write-only, cannot read
        fclose(fp);
    }
}

int main() { return 0; }
