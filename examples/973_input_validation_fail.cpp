// 973_input_validation_fail.cpp
// Tests: Input validation - unescaped SQL injection caught
// From BLOG.md: Input Validation example

struct UserInput {
    [[clang::annotate("dtt:ensures=_sql_escaped == 0")]]
    UserInput(const char* raw) {}

    [[clang::annotate("dtt:ensures=_sql_escaped == 1")]]
    void escape_sql() {}

    [[clang::annotate("dtt:requires=_sql_escaped == 1")]]
    const char* as_sql_param() { return ""; }
};

void test_sql_injection() {
    UserInput input("Robert'); DROP TABLE Students;--");
    // NOT calling input.escape_sql()
    input.as_sql_param();  // ERROR: _sql_escaped is 0, requires 1
}

int main() {
    test_sql_injection();
    return 0;
}
