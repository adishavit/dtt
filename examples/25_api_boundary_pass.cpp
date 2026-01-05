// Example 14: API boundary annotations — should COMPILE
// Real-world pattern: wrap opaque types at trust boundaries

// Opaque handle from external library (can't see inside)
struct DBResult { void* handle; };

// Tracked wrapper for compile-time verification
struct SafeDBResult {
    DBResult inner;
    int row_count;  // DTT tracks this
};

// === API Boundary: External library calls ===

// Simulated DB query — returns result with known count
SafeDBResult query(const char* sql) {
    SafeDBResult r;
    r.row_count = 0;  // Unknown until we check
    return r;
}

// After COUNT(*), we know exact row count
[[clang::annotate("dtt:ensures=result.row_count == arg0.row_count")]]
SafeDBResult withCount(SafeDBResult& r, int count);

// === Application code with preconditions ===

[[clang::annotate("dtt:requires=row_count > 0")]]
void* getFirstRow(SafeDBResult& r) { return r.inner.handle; }

[[clang::annotate("dtt:requires=row_count >= 10")]]
void processBatch(SafeDBResult& r) { (void)r; }

int main() {
    // Query returns unknown count
    SafeDBResult users = query("SELECT * FROM users");

    // Can't use yet:
    // getFirstRow(users);  // Would fail: row_count unknown

    // API boundary: we checked count externally
    users.row_count = 42;  // Simulate: DB told us there are 42 rows

    getFirstRow(users);    // ✓ OK: 42 > 0
    processBatch(users);   // ✓ OK: 42 >= 10

    // Or use runtime guard
    SafeDBResult posts = query("SELECT * FROM posts");
    if (posts.row_count > 0) {
        getFirstRow(posts);  // ✓ OK: guard proves row_count > 0
    }

    return 0;
}
