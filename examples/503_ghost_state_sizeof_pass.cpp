// Example 503: Ghost state (@) has zero runtime storage vs runtime fields (_)
// Compile and run to see sizeof() differences

#include <cstdio>

// Runtime field (_) - adds storage
struct FileHandle_Runtime {
    int fd;
    bool _is_open;  // Takes 1+ bytes (plus padding)

    [[clang::annotate("dtt:ensures=_is_open == 1")]]
    void open(const char* path) { fd = 1; _is_open = true; }

    [[clang::annotate("dtt:requires=_is_open == 1")]]
    void write(const char* data) {}
};

// Ghost state (@) - zero storage
struct FileHandle_Ghost {
    int fd;
    // No @is_open field exists in memory!

    [[clang::annotate("dtt:ensures=@is_open == 1")]]
    void open(const char* path) { fd = 1; }

    [[clang::annotate("dtt:requires=@is_open == 1")]]
    void write(const char* data) {}
};

// Multiple ghost properties - still zero overhead
struct SecureChannel {
    int socket_fd;
    // Ghost: @authenticated, @encrypted, @key_id - none exist in memory!

    [[clang::annotate("dtt:ensures=@authenticated == 1")]]
    void authenticate() {}

    [[clang::annotate("dtt:requires=@authenticated == 1"),
      clang::annotate("dtt:ensures=@encrypted == 1"),
      clang::annotate("dtt:ensures=@key_id == arg0")]]
    void encrypt(int key) {}

    [[clang::annotate("dtt:requires=@encrypted == 1"),
      clang::annotate("dtt:requires=@key_id == arg0")]]
    void send(int key, const char* data) {}
};

int main() {
    // sizeof comparison
    static_assert(sizeof(FileHandle_Runtime) > sizeof(FileHandle_Ghost),
                  "Runtime field takes space, ghost state is free");
    static_assert(sizeof(SecureChannel) == sizeof(int),
                  "3 ghost properties add 0 bytes");

    // Both provide the same compile-time safety
    FileHandle_Ghost f;
    f.open("test.txt");
    f.write("data");  // OK: @is_open == 1

    SecureChannel ch;
    ch.authenticate();
    ch.encrypt(42);
    ch.send(42, "secret");  // OK: all ghost state satisfied

    return 0;
}
