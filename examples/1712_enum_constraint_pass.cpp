// Test: Named enumerations in .dtt annotations (Gap 5)
// Verifies that enum constants resolve correctly in requires, ensures,
// ensures_if_true/false, and free function annotations.

// Stub type matching EnumBuf from test_enum.dtt
class EnumBuf {
public:
    EnumBuf() {}
    void fill_small() {}
    void fill_large() {}
    int get(int idx) { return 0; }
    bool is_empty() { return true; }
    void push_back(int val) {}
};

// Stubs for free functions
struct EFile {};
EFile* enum_fopen(const char* path);
void enum_fclose(EFile* f);

// Variant 1: enum in ensures + requires — basic flow
void test_enum_fill_and_get() {
    EnumBuf b;            // _size = Capacity.EMPTY (0)
    b.fill_small();       // _size = Capacity.SMALL (1)
    b.get(0);             // requires _size != Capacity.EMPTY → 1 != 0 OK
}

// Variant 2: enum with larger value
void test_enum_fill_large() {
    EnumBuf b;            // _size = 0
    b.fill_large();       // _size = Capacity.LARGE (10)
    b.get(5);             // 10 != 0 OK, 5 < 10 OK
}

// Variant 3: enum in ensures via push_back + old()
void test_enum_push_and_get() {
    EnumBuf b;            // _size = 0
    b.push_back(42);      // _size = 0 + 1 = 1
    b.get(0);             // 1 != 0 OK, 0 < 1 OK
}

// Variant 4: enum in ensures_if_false — is_empty returns false on non-empty
void test_enum_is_empty_false_branch() {
    EnumBuf b;
    b.fill_small();       // _size = 1
    if (!b.is_empty()) {  // ensures_if_false: _size > Capacity.EMPTY → _size > 0
        b.get(0);         // _size > 0, 0 < _size OK
    }
}

// Variant 5: enum in free function ensures/requires/scope_exit
void test_enum_free_func() {
    EFile* f = enum_fopen("test.txt");  // @eopen = FileState.OPEN (1)
    if (f) {
        enum_fclose(f);                 // requires @eopen != FileState.CLOSED → 1 != 0 OK
                                        // ensures @eopen = FileState.CLOSED (0)
    }
    // scope_exit: @eopen == FileState.CLOSED (0) — satisfied
}
