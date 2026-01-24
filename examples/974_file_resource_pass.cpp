// 974_file_resource_pass.cpp
// Tests: File resource tracking - open/close state and read/write mode
// From BLOG.md: File Resources example

struct File {
    int _open;
    int _readable;
    int _writable;

    [[clang::annotate("dtt:requires=_open == 1")]]
    [[clang::annotate("dtt:requires=_readable == 1")]]
    void read(char* buf, int len) {}

    [[clang::annotate("dtt:requires=_open == 1")]]
    [[clang::annotate("dtt:requires=_writable == 1")]]
    void write(const char* data) {}

    [[clang::annotate("dtt:requires=_open == 1")]]
    [[clang::annotate("dtt:ensures=_open == 0")]]
    void close() {}
};

// Variant 1: Read from read-only file
void test_read_file() {
    File f;
    f._open = 1;
    f._readable = 1;
    f._writable = 0;

    char buf[100];
    f.read(buf, 100);    // OK: _readable == 1
    f.close();
}

// Variant 2: Write to write-only file
void test_write_file() {
    File f;
    f._open = 1;
    f._readable = 0;
    f._writable = 1;

    f.write("hello");    // OK: _writable == 1
    f.close();
}

// Variant 3: Read and write with readwrite mode
void test_readwrite_file() {
    File f;
    f._open = 1;
    f._readable = 1;
    f._writable = 1;

    char buf[100];
    f.read(buf, 100);    // OK: _readable == 1
    f.write("updated");  // OK: _writable == 1
    f.close();
}

int main() {
    test_read_file();
    test_write_file();
    test_readwrite_file();
    return 0;
}
