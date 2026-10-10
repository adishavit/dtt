// Test: Named enumerations — violation detected
// EnumBuf::get requires _size != Capacity.EMPTY, but _size is 0 after ctor.

class EnumBuf {
public:
    EnumBuf() {}
    int get(int idx) { return 0; }
};

// Enum requires violation: _size = EMPTY (0), get requires _size != EMPTY
void test_enum_empty_get() {
    EnumBuf b;            // _size = Capacity.EMPTY (0)
    b.get(0);             // ERROR: _size != Capacity.EMPTY → 0 != 0 fails  dtt-error {{_size != 0}} dtt-error {{out of bounds}}
}
