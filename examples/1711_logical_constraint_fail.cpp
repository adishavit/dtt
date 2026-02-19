// Test: Compound preconditions with && - violation detected
// When using &&, failing EITHER side should trigger an error.

// Stub type matching RangeBuffer annotation from test_logical.dtt
class RangeBuffer {
public:
    RangeBuffer() {}
    void safe_get(int idx);
    int size();
    void push_back(int val);
};

// && violation: _size == 0, so _size > 0 fails (left side of &&)
void test_and_empty_buffer() {
    RangeBuffer buf;          // _size = 0
    buf.safe_get(0);          // ERROR: _size > 0 fails
}
