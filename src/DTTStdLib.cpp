// DTTStdLib.cpp - STL type annotations implementation for DTT

#include "DTTStdLib.h"

namespace dtt {

llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;

// Template parameter to property mappings
// Maps template parameters to semantic properties (e.g., std::array<T,5> → _size = 5)
std::map<std::string, std::vector<TemplateParamAlias>> TemplateAliases = {
    {"std::array",  {{1, "_size", std::nullopt}}},
    {"std::span",   {{1, "_size", static_cast<int64_t>(-1)}}},  // skip dynamic_extent
    {"std::bitset", {{0, "_size", std::nullopt}}},
};

void registerSyntheticAnnotations() {
    // =========================================================================
    // Constraint expression factories
    // =========================================================================

    auto boundsCheck = []() -> ConstraintExprPtr {
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpLT,
            std::make_shared<ArgRefExpr>(0),
            std::make_shared<FieldRefExpr>("_size"));
    };

    auto nonEmpty = []() -> ConstraintExprPtr {
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpGT,
            std::make_shared<FieldRefExpr>("_size"),
            std::make_shared<LiteralExpr>(0));
    };

    auto hasValue = []() -> ConstraintExprPtr {
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpEQ,
            std::make_shared<FieldRefExpr>("_has_value"),
            std::make_shared<LiteralExpr>(1));
    };

    auto nonnull = []() -> ConstraintExprPtr {
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpEQ,
            std::make_shared<FieldRefExpr>("_nonnull"),
            std::make_shared<LiteralExpr>(1));
    };

    // =========================================================================
    // Registration helpers
    // =========================================================================

    auto addPre = [](const char* cls, const char* method, unsigned arity, ConstraintExprPtr pre) {
        MethodInfo info;
        info.preconditions.push_back(pre);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    auto addReturns = [](const char* cls, const char* method, const char* field) {
        MethodInfo info;
        info.returns = field;
        MethodAnnotations[{cls, method, 0}] = info;
    };

    auto addBoolPost = [](const char* cls, const char* method, const char* field, int trueVal, int falseVal) {
        MethodInfo info;
        info.boolPostconds.push_back({"", field, ProofOp::EQ, trueVal, true});
        info.boolPostconds.push_back({"", field, ProofOp::EQ, falseVal, false});
        MethodAnnotations[{cls, method, 0}] = info;
    };

    auto addPostConst = [](const char* cls, const char* method, unsigned arity, const char* field, int64_t val) {
        MethodInfo info;
        Postcondition post;
        post.resultField = field;
        post.expr = SymValue::Const(val);
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    auto addPostArg0 = [](const char* cls, const char* method, unsigned arity, const char* field) {
        MethodInfo info;
        Postcondition post;
        post.resultField = field;
        post.expr = SymValue::Var("arg0");
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    auto addPostArg1 = [](const char* cls, const char* method, unsigned arity, const char* field) {
        MethodInfo info;
        Postcondition post;
        post.resultField = field;
        post.expr = SymValue::Var("arg1");
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    auto addSizeIncrement = [](const char* cls, const char* method, unsigned arity) {
        MethodInfo info;
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Add(SymValue::Var("old:_size"), SymValue::Const(1));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    auto addSizePreserving = [](const char* cls, const char* method, unsigned arity) {
        MethodInfo info;
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Var("old:_size");
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    auto addPop = [&nonEmpty](const char* cls, const char* method) {
        MethodInfo info;
        info.preconditions.push_back(nonEmpty());
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Sub(SymValue::Var("old:_size"), SymValue::Const(1));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, 0}] = info;
    };

    // =========================================================================
    // Batch registration for common patterns
    // =========================================================================

    // Containers with bounds checking via operator[]
    for (const char* cls : {"array", "span", "bitset"}) {
        addPre(cls, "operator[]", 1, boundsCheck());
    }
    for (const char* cls : {"array", "span"}) {
        addPre(cls, "front", 0, nonEmpty());
        addPre(cls, "back", 0, nonEmpty());
    }

    // std::span constructor (ptr, count)
    addPostArg1("span", "span", 2, "_size");

    // Containers with full size tracking: constructor, size(), empty(), clear()
    for (const char* cls : {"vector", "deque", "list", "forward_list",
                            "set", "multiset", "map", "multimap",
                            "unordered_set", "unordered_multiset",
                            "unordered_map", "unordered_multimap",
                            "stack", "queue", "priority_queue"}) {
        addPostConst(cls, cls, 0, "_size", 0);
        addBoolPost(cls, "empty", "_size", 0, 1);  // empty() true ↔ _size==0
        addPostConst(cls, "clear", 0, "_size", 0);
    }

    // Containers that expose size()
    for (const char* cls : {"vector", "deque", "list",
                            "set", "multiset", "map", "multimap",
                            "unordered_set", "unordered_multiset",
                            "unordered_map", "unordered_multimap",
                            "stack", "queue", "priority_queue"}) {
        addReturns(cls, "size", "_size");
    }

    // Sequence containers with operator[], front(), back()
    for (const char* cls : {"vector", "basic_string", "deque"}) {
        addPre(cls, "operator[]", 1, boundsCheck());
        addPre(cls, "front", 0, nonEmpty());
        addPre(cls, "back", 0, nonEmpty());
    }

    // Containers with front() only
    for (const char* cls : {"list", "forward_list", "queue"}) {
        addPre(cls, "front", 0, nonEmpty());
    }

    // Containers with front() and back()
    for (const char* cls : {"list", "queue"}) {
        addPre(cls, "back", 0, nonEmpty());
    }

    // Containers with top()
    for (const char* cls : {"stack", "priority_queue"}) {
        addPre(cls, "top", 0, nonEmpty());
    }

    // Containers with pop_front() and pop_back()
    for (const char* cls : {"deque", "list"}) {
        addPre(cls, "pop_front", 0, nonEmpty());
        addPre(cls, "pop_back", 0, nonEmpty());
    }
    addPre("forward_list", "pop_front", 0, nonEmpty());

    // Containers with push_back/emplace_back
    for (const char* cls : {"vector", "deque", "list"}) {
        addSizeIncrement(cls, "push_back", 1);
        for (unsigned i = 0; i <= 3; i++) addSizeIncrement(cls, "emplace_back", i);
        addPop(cls, "pop_back");
    }

    // Containers with push_front/emplace_front
    for (const char* cls : {"deque", "list", "forward_list"}) {
        addSizeIncrement(cls, "push_front", 1);
        for (unsigned i = 0; i <= 3; i++) addSizeIncrement(cls, "emplace_front", i);
    }

    // Containers with insert(pos, value)
    for (const char* cls : {"vector", "deque", "list"}) {
        addSizeIncrement(cls, "insert", 2);
    }

    // Containers with resize(n) and resize(n, value)
    for (const char* cls : {"vector", "deque", "list"}) {
        addPostArg0(cls, "resize", 1, "_size");
        addPostArg0(cls, "resize", 2, "_size");
    }

    // Containers with assign(count, value) - sets size to count
    for (const char* cls : {"vector", "deque", "list"}) {
        addPostArg0(cls, "assign", 2, "_size");
    }

    // Containers with insert(pos, count, value) - adds count to size
    for (const char* cls : {"vector", "deque", "list"}) {
        MethodInfo info;
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Add(SymValue::Var("old:_size"), SymValue::Var("arg1"));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{cls, "insert", 3}] = info;
    }

    // Containers with emplace(pos, args...) - single element insert
    for (const char* cls : {"vector", "deque", "list"}) {
        for (unsigned i = 1; i <= 4; i++) addSizeIncrement(cls, "emplace", i);
    }

    // Containers with erase(pos) - requires non-empty, decrements size (1 arg = iterator)
    for (const char* cls : {"vector", "deque", "list"}) {
        MethodInfo info;
        info.preconditions.push_back(nonEmpty());
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Sub(SymValue::Var("old:_size"), SymValue::Const(1));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{cls, "erase", 1}] = info;
    }

    // Iterator methods preserve size
    for (const char* cls : {"vector", "basic_string", "deque", "list"}) {
        for (const char* method : {"begin", "end", "rbegin", "rend"}) {
            addSizePreserving(cls, method, 0);
        }
    }
    addSizePreserving("vector", "data", 0);
    addSizePreserving("basic_string", "data", 0);
    addSizePreserving("basic_string", "c_str", 0);

    // Adapter containers with push/emplace
    for (const char* cls : {"stack", "queue", "priority_queue"}) {
        addSizeIncrement(cls, "push", 1);
        for (unsigned i = 0; i <= 3; i++) addSizeIncrement(cls, "emplace", i);
        addPre(cls, "pop", 0, nonEmpty());
    }

    // Set containers with insert(value)
    for (const char* cls : {"set", "multiset", "unordered_set", "unordered_multiset"}) {
        addSizeIncrement(cls, "insert", 1);
    }

    // Map containers with at(key) - requires non-empty (catches at() on empty map)
    // Note: Cannot verify key existence, but this catches the obvious case
    for (const char* cls : {"map", "unordered_map"}) {
        addPre(cls, "at", 1, nonEmpty());
    }

    // =========================================================================
    // std::basic_string specifics
    // =========================================================================

    addPostConst("basic_string", "basic_string", 0, "_size", 0);
    // Note: The 2-arg constructor (const char*, allocator) is handled specially in DTTPlugin.cpp
    // to detect string literal length. Don't register a generic annotation here.
    addBoolPost("basic_string", "empty", "_size", 0, 1);
    addReturns("basic_string", "size", "_size");
    addReturns("basic_string", "length", "_size");
    addSizeIncrement("basic_string", "push_back", 1);
    addSizeIncrement("basic_string", "operator+=", 1);  // char overload; string literal handled in plugin
    addPop("basic_string", "pop_back");
    addPostConst("basic_string", "clear", 0, "_size", 0);
    addPostArg0("basic_string", "resize", 1, "_size");
    addPostArg0("basic_string", "resize", 2, "_size");
    addPostArg0("basic_string", "assign", 2, "_size");  // assign(count, char)
    // append(count, char) - adds count characters
    {
        MethodInfo info;
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Add(SymValue::Var("old:_size"), SymValue::Var("arg0"));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{"basic_string", "append", 2}] = info;
    }

    // std::basic_string_view
    addPostConst("basic_string_view", "basic_string_view", 0, "_size", 0);  // default constructor
    addPostArg1("basic_string_view", "basic_string_view", 2, "_size");       // (ptr, count) constructor
    addPre("basic_string_view", "operator[]", 1, boundsCheck());
    addPre("basic_string_view", "front", 0, nonEmpty());
    addPre("basic_string_view", "back", 0, nonEmpty());
    addReturns("basic_string_view", "size", "_size");
    addReturns("basic_string_view", "length", "_size");
    addBoolPost("basic_string_view", "empty", "_size", 0, 1);
    // remove_prefix(n) and remove_suffix(n) subtract n from _size
    {
        MethodInfo info;
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Sub(SymValue::Var("old:_size"), SymValue::Var("arg0"));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{"basic_string_view", "remove_prefix", 1}] = info;
        MethodAnnotations[{"basic_string_view", "remove_suffix", 1}] = info;
    }

    // std::vector specifics
    addPostArg0("vector", "vector", 1, "_size");
    addPostArg0("vector", "vector", 2, "_size");
    addPostArg0("vector", "vector", 3, "_size");  // (count, value, allocator)

    // =========================================================================
    // Optional/nullable types
    // =========================================================================

    // std::optional
    addPostConst("optional", "optional", 0, "_has_value", 0);
    addPostConst("optional", "optional", 1, "_has_value", 1);
    addPre("optional", "value", 0, hasValue());
    addPre("optional", "operator*", 0, hasValue());
    addPre("optional", "operator->", 0, hasValue());
    addBoolPost("optional", "has_value", "_has_value", 1, 0);
    addBoolPost("optional", "operator bool", "_has_value", 1, 0);
    addPostConst("optional", "reset", 0, "_has_value", 0);
    for (unsigned i = 0; i <= 3; i++) addPostConst("optional", "emplace", i, "_has_value", 1);
    addPostConst("optional", "operator=", 1, "_has_value", 1);

    // std::variant
    addBoolPost("variant", "valueless_by_exception", "_has_value", 0, 1);

    // std::any
    addPostConst("any", "any", 0, "_has_value", 0);
    addPostConst("any", "any", 1, "_has_value", 1);
    addBoolPost("any", "has_value", "_has_value", 1, 0);
    addPostConst("any", "reset", 0, "_has_value", 0);
    addPostConst("any", "operator=", 1, "_has_value", 1);
    for (unsigned i = 0; i <= 3; i++) addPostConst("any", "emplace", i, "_has_value", 1);

    // =========================================================================
    // Smart pointers
    // =========================================================================

    for (const char* cls : {"unique_ptr", "shared_ptr"}) {
        addPostConst(cls, cls, 0, "_nonnull", 0);
        addPostConst(cls, cls, 1, "_nonnull", 1);
        addPre(cls, "operator*", 0, nonnull());
        addPre(cls, "operator->", 0, nonnull());
        addBoolPost(cls, "operator bool", "_nonnull", 1, 0);
        addPostConst(cls, "reset", 0, "_nonnull", 0);
    }
    addPostConst("unique_ptr", "reset", 1, "_nonnull", 1);
    addPostConst("unique_ptr", "release", 0, "_nonnull", 0);

    // std::weak_ptr
    addBoolPost("weak_ptr", "expired", "_nonnull", 0, 1);
}

} // namespace dtt
