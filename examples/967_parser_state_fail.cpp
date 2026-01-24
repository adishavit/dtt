// 967_parser_state_fail.cpp
// Tests: Parser state tracking - consume more than remaining
// From BLOG.md: Parser State example

struct ParseState {
    [[clang::annotate("dtt:ensures=_pos == 0")]]
    [[clang::annotate("dtt:ensures=_remaining == arg0")]]
    ParseState(int input_len) {}

    [[clang::annotate("dtt:requires=arg0 <= _remaining")]]
    [[clang::annotate("dtt:ensures=_remaining == old(_remaining) - arg0")]]
    void consume(int n) {}

    [[clang::annotate("dtt:requires=_remaining == 0")]]
    void expect_eof() {}
};

void test_consume_too_much() {
    ParseState p(100);     // _remaining = 100
    p.consume(50);         // _remaining = 50
    p.consume(60);         // ERROR: 60 > _remaining(50)
}

int main() {
    test_consume_too_much();
    return 0;
}
