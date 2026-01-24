// 972_input_validation_pass.cpp
// Tests: Input validation - proper validation before use
// From BLOG.md: Input Validation example

struct UserInput {
    [[clang::annotate("dtt:ensures=_validated == 0")]]
    [[clang::annotate("dtt:ensures=_is_email == 0")]]
    [[clang::annotate("dtt:ensures=_sql_escaped == 0")]]
    UserInput(const char* raw) {}

    [[clang::annotate("dtt:ensures=_validated == 1")]]
    [[clang::annotate("dtt:ensures=_is_email == 1")]]
    void validate_email() {}

    [[clang::annotate("dtt:ensures=_sql_escaped == 1")]]
    void escape_sql() {}

    [[clang::annotate("dtt:requires=_is_email == 1")]]
    void send_to(const char* smtp_server) {}

    [[clang::annotate("dtt:requires=_sql_escaped == 1")]]
    const char* as_sql_param() { return ""; }
};

// Variant 1: Validate email before sending
void test_validated_email() {
    UserInput input("user@example.com");
    input.validate_email();           // _is_email = 1
    input.send_to("mail.server.com"); // OK: _is_email == 1
}

// Variant 2: Escape SQL before query
void test_escaped_sql() {
    UserInput input("Robert'); DROP TABLE Students;--");
    input.escape_sql();               // _sql_escaped = 1
    input.as_sql_param();             // OK: _sql_escaped == 1
}

// Variant 3: Both validations
void test_both_validations() {
    UserInput input("admin@company.com");
    input.validate_email();
    input.escape_sql();
    input.send_to("smtp.company.com");
    input.as_sql_param();             // Both OK
}

int main() {
    test_validated_email();
    test_escaped_sql();
    test_both_validations();
    return 0;
}
