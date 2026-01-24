// 966_parser_state_pass.cpp
// Tests: Parser state tracking - position and remaining bytes
// From BLOG.md: Parser State example

struct ParseState {
    [[clang::annotate("dtt:ensures=_pos == 0")]]
    [[clang::annotate("dtt:ensures=_len == arg0")]]
    [[clang::annotate("dtt:ensures=_remaining == arg0")]]
    ParseState(int input_len) {}

    [[clang::annotate("dtt:requires=arg0 <= _remaining")]]
    [[clang::annotate("dtt:ensures=_pos == old(_pos) + arg0")]]
    [[clang::annotate("dtt:ensures=_remaining == old(_remaining) - arg0")]]
    void consume(int n) {}

    [[clang::annotate("dtt:requires=arg0 < _remaining")]]
    char peek(int offset) { return 0; }

    [[clang::annotate("dtt:requires=_remaining == 0")]]
    void expect_eof() {}
};

// Variant 1: Consume all input then expect EOF
void test_consume_all() {
    ParseState p(100);     // _remaining = 100
    p.consume(50);         // _remaining = 50
    p.consume(50);         // _remaining = 0
    p.expect_eof();        // OK: _remaining == 0
}

// Variant 2: Peek within bounds
void test_peek_valid() {
    ParseState p(10);
    p.peek(0);             // OK: 0 < 10
    p.peek(9);             // OK: 9 < 10
    p.consume(5);          // _remaining = 5
    p.peek(4);             // OK: 4 < 5
}

// Variant 3: Incremental parsing
void test_incremental() {
    ParseState p(20);
    p.consume(5);          // _remaining = 15
    p.consume(5);          // _remaining = 10
    p.consume(5);          // _remaining = 5
    p.consume(5);          // _remaining = 0
    p.expect_eof();        // OK
}

int main() {
    test_consume_all();
    test_peek_valid();
    test_incremental();
    return 0;
}
