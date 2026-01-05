// Example 15: API boundary — should FAIL
// Catches: using data without verifying at boundary

struct DBResult { void* handle; };

struct SafeDBResult {
    DBResult inner;
    int row_count;
};

SafeDBResult query(const char* sql) {
    SafeDBResult r;
    r.row_count = 0;
    return r;
}

[[clang::annotate("dtt:requires=row_count > 0")]]
void* getFirstRow(SafeDBResult& r) { return r.inner.handle; }

int main() {
    SafeDBResult users = query("SELECT * FROM users");

    // Bug: forgot to check/set row_count at API boundary
    getFirstRow(users);  // ERROR: row_count unknown

    return 0;
}
