// ============================================================
// C++26 Contracts → DTT Auto-Inferred Annotations
// ============================================================
//
// These examples show standard C++26 contracts on the left,
// with the equivalent DTT annotation that would be auto-inferred
// shown in comments. The programmer writes ONLY the contract;
// DTT produces the annotation internally via Expr* translation.
//
// NOT compilable today — requires Clang with P2900 support.
// ============================================================

#include <vector>
#include <optional>
#include <string>
#include <memory>
#include <variant>
#include <ranges>
#include <cstdio>

// ============================================================
// 1. BASIC CONTAINER PRECONDITIONS
// ============================================================

// --- .empty() negation → _size > 0 ---

int front(std::vector<int>& v)
    pre(!v.empty())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
{
    return v.front();
}

// --- .size() comparison → _size constraint ---

int safe_access(std::vector<int>& v, int idx)
    pre(v.size() > 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
    pre(idx >= 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 >= 0")]]
    pre(idx < v.size())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 < _size")]]
{
    return v[idx];
}

// --- Arithmetic on .size() ---

int second_to_last(std::vector<int>& v)
    pre(v.size() >= 2)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size >= 2")]]
{
    return v[v.size() - 2];
}

// ============================================================
// 2. POINTER PRECONDITIONS
// ============================================================

// --- nullptr comparison → _nonnull ---

void write_to(int* p, int val)
    pre(p != nullptr)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_nonnull != 0")]]
{
    *p = val;
}

int read_from(const int* p)
    pre(p != nullptr)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_nonnull != 0")]]
{
    return *p;
}

// --- Smart pointer .get() ---

void process(const std::unique_ptr<int>& p)
    pre(p != nullptr)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_nonnull != 0")]]
{
    *p = 42;
}

// ============================================================
// 3. OPTIONAL PRECONDITIONS
// ============================================================

// --- .has_value() → _has_value ---

int unwrap(std::optional<int>& o)
    pre(o.has_value())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_has_value != 0")]]
{
    return o.value();
}

// --- operator bool (implicit) ---

int unwrap2(std::optional<int>& o)
    pre(o)  // optional's operator bool
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_has_value != 0")]]
{
    return *o;
}

// ============================================================
// 4. POSTCONDITIONS
// ============================================================

// --- Ensures container non-empty after operation ---

void add_element(std::vector<int>& v, int x)
    post(!v.empty())
    // AUTO-INFERRED: [[clang::annotate("dtt:ensures=_size > 0")]]
{
    v.push_back(x);
}

// --- Ensures on return value ---

int clamp_positive(int x)
    post(r : r > 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:ensures=_result > 0")]]
{
    return x > 0 ? x : 1;
}

// --- Ensures optional has value ---

std::optional<int> make_value(int x)
    post(r : r.has_value())
    // AUTO-INFERRED: [[clang::annotate("dtt:ensures=result._has_value != 0")]]
{
    return x;
}

// --- Ensures return pointer non-null ---

int* allocate_int()
    post(r : r != nullptr)
    // AUTO-INFERRED: [[clang::annotate("dtt:ensures=result._nonnull != 0")]]
{
    return new int(0);
}

// ============================================================
// 5. COMPOUND CONTRACTS
// ============================================================

// --- Multiple preconditions (logical AND) ---

int safe_divide(std::vector<int>& v, int idx, int divisor)
    pre(!v.empty())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
    pre(idx >= 0 && idx < v.size())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 >= 0 && arg1 < _size")]]
    pre(divisor != 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg2 != 0")]]
{
    return v[idx] / divisor;
}

// --- Pre + Post combined ---

int pop_front(std::vector<int>& v)
    pre(v.size() > 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
    post(r : true)  // could express more with old()
    // (no old() in C++26 contracts — see §6.3 of proposal)
{
    int val = v.front();
    v.erase(v.begin());
    return val;
}

// ============================================================
// 6. POSTCONDITION WITH DISJUNCTION (nullable returns)
// ============================================================

// --- Result is either null or valid ---

std::optional<int> find_in(const std::vector<int>& v, int target)
    post(r : !r.has_value() || r.value() >= 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:ensures=result._has_value == 0 || result._value >= 0")]]
{
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == target) return static_cast<int>(i);
    return std::nullopt;
}

// ============================================================
// 7. contract_assert AS PROOF HINT
// ============================================================

void process_range(std::vector<int>& v, int start, int end) {
    // ... complex logic establishing bounds ...
    contract_assert(start >= 0);
    // AUTO-INFERRED: proof fact: start >= 0
    contract_assert(end <= v.size());
    // AUTO-INFERRED: proof fact: end <= v._size
    contract_assert(start < end);
    // AUTO-INFERRED: proof fact: start < end

    for (int i = start; i < end; ++i) {
        v[i] = 0;  // DTT proves safe: i >= start >= 0, i < end <= _size
    }
}

// ============================================================
// 8. CONCEPT-INFORMED GENERIC CODE
// ============================================================

// --- sized_range tells DTT that _size is trackable ---
// --- pre uses .size()/.empty() which DTT maps to _size ---

template<std::ranges::sized_range R>
auto first_element(R& r)
    pre(!std::ranges::empty(r))
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
{
    return *std::ranges::begin(r);
}

template<std::ranges::random_access_range R>
auto safe_index(R& r, std::ranges::range_difference_t<R> idx)
    pre(std::ranges::size(r) > 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
    pre(idx >= 0)
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 >= 0")]]
    pre(idx < std::ranges::ssize(r))
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 < _size")]]
{
    return r[idx];
}

// ============================================================
// 9. REAL-WORLD PATTERNS
// ============================================================

// --- String processing ---

char first_char(const std::string& s)
    pre(!s.empty())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
{
    return s[0];
}

std::string safe_substr(const std::string& s, size_t pos, size_t len)
    pre(pos < s.size())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 < _size")]]
    pre(pos + len <= s.size())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 + arg2 <= _size")]]
{
    return s.substr(pos, len);
}

// --- Variant access ---

int get_int(const std::variant<int, double, std::string>& v)
    pre(std::holds_alternative<int>(v))
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_variant_index == 0")]]
{
    return std::get<int>(v);
}

// --- Matrix-like: nested container ---

int get_cell(const std::vector<std::vector<int>>& grid, int row, int col)
    pre(!grid.empty())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=_size > 0")]]
    pre(row >= 0 && row < grid.size())
    // AUTO-INFERRED: [[clang::annotate("dtt:requires=arg1 >= 0 && arg1 < _size")]]
    // Note: col bounds on grid[row] would need deeper analysis (future work)
{
    return grid[row][col];
}

// ============================================================
// 10. WHAT CONTRACTS CANNOT EXPRESS (remains annotation-only)
// ============================================================

// Ghost fields: no C++ surface for @open, @allocated, @fd
// These stay as [[clang::annotate]]:

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE* open_log(const char* path);

// old() values: C++26 contracts have no old() construct
// These stay as [[clang::annotate]]:

[[clang::annotate("dtt:ensures=_size == old(_size) + 1")]]
void tracked_push(std::vector<int>& v, int x);

// ============================================================
// 11. CALL-SITE VERIFICATION (what the caller sees)
// ============================================================

void demo() {
    // --- Container contracts ---
    std::vector<int> v;
    // front(v);                // DTT ERROR (F): v._size == 0, requires > 0
    v.push_back(42);
    front(v);                   // DTT SAFE (E): v._size == 1 > 0 ✓
    safe_access(v, 0);          // DTT SAFE (E): _size==1 > 0, 0 >= 0, 0 < 1 ✓
    // safe_access(v, 5);       // DTT ERROR (F): 5 < 1 is false

    // --- Pointer contracts ---
    int* p = new int(10);
    write_to(p, 20);            // DTT SAFE (E): p from new, _nonnull ✓
    int* q = nullptr;
    // write_to(q, 20);         // DTT ERROR (F): q == nullptr

    // --- Optional contracts ---
    std::optional<int> o;
    // unwrap(o);               // DTT ERROR (F): _has_value == 0
    o = 42;
    unwrap(o);                  // DTT SAFE (E): _has_value != 0 ✓

    // --- Postcondition flows into subsequent code ---
    auto val = make_value(7);   // DTT knows: val._has_value != 0 (from post)
    unwrap(val);                // DTT SAFE (E): postcondition guarantees _has_value ✓

    // --- Variant contracts ---
    std::variant<int, double, std::string> var = 42;
    get_int(var);               // DTT SAFE (E): _variant_index == 0 ✓
    var = 3.14;
    // get_int(var);            // DTT ERROR (F): _variant_index == 1, requires 0

    delete p;
}
