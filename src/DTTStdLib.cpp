// DTTStdLib.cpp - STL type annotations implementation for DTT

#include "DTTStdLib.h"

namespace dtt {

// Definition of the global method annotations map
llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;

void registerSyntheticAnnotations() {
    // =========================================================================
    // Helper lambdas for creating common patterns
    // =========================================================================

    auto boundsCheck = []() -> ConstraintExprPtr { // arg0 < _size
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpLT,
            std::make_shared<ArgRefExpr>(0),
            std::make_shared<FieldRefExpr>("_size"));
    };

    auto nonEmpty = []() -> ConstraintExprPtr { // _size > 0
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpGT,
            std::make_shared<FieldRefExpr>("_size"),
            std::make_shared<LiteralExpr>(0));
    };

    auto hasValue = []() -> ConstraintExprPtr { // _has_value == 1
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpEQ,
            std::make_shared<FieldRefExpr>("_has_value"),
            std::make_shared<LiteralExpr>(1));
    };

    auto nonnull = []() -> ConstraintExprPtr { // _nonnull == 1
        return std::make_shared<BinaryExpr>(ConstraintExpr::CmpEQ,
            std::make_shared<FieldRefExpr>("_nonnull"),
            std::make_shared<LiteralExpr>(1));
    };

    // Helper to register precondition-only method
    auto addPre = [](const char* cls, const char* method, unsigned arity, ConstraintExprPtr pre) {
        MethodInfo info;
        info.preconditions.push_back(pre);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    // Helper to register method with returns field
    auto addReturns = [](const char* cls, const char* method, const char* field) {
        MethodInfo info;
        info.returns = field;
        MethodAnnotations[{cls, method, 0}] = info;
    };

    // Helper for empty() style bool postconditions
    auto addSizeEmpty = [](const char* cls) {
        MethodInfo info;
        info.boolPostconds.push_back({"", "_size", ProofOp::EQ, 0, true});
        info.boolPostconds.push_back({"", "_size", ProofOp::GT, 0, false});
        MethodAnnotations[{cls, "empty", 0}] = info;
    };

    // Helper for has_value style bool postconditions
    auto addHasValueBool = [](const char* cls, const char* method) {
        MethodInfo info;
        info.boolPostconds.push_back({"", "_has_value", ProofOp::EQ, 1, true});
        info.boolPostconds.push_back({"", "_has_value", ProofOp::EQ, 0, false});
        MethodAnnotations[{cls, method, 0}] = info;
    };

    // Helper for nonnull style bool postconditions
    auto addNonnullBool = [](const char* cls, const char* method) {
        MethodInfo info;
        info.boolPostconds.push_back({"", "_nonnull", ProofOp::EQ, 1, true});
        info.boolPostconds.push_back({"", "_nonnull", ProofOp::EQ, 0, false});
        MethodAnnotations[{cls, method, 0}] = info;
    };

    // Helper for postcondition setting field to constant
    auto addPostConst = [](const char* cls, const char* method, unsigned arity, const char* field, int64_t val) {
        MethodInfo info;
        Postcondition post;
        post.resultField = field;
        post.expr = SymValue::Const(val);
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    // Helper for postcondition setting field to arg0
    auto addPostArg0 = [](const char* cls, const char* method, unsigned arity, const char* field) {
        MethodInfo info;
        Postcondition post;
        post.resultField = field;
        post.expr = SymValue::Var("arg0");
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    // Helper for size increment: _size = old(_size) + 1
    auto addSizeIncrement = [](const char* cls, const char* method, unsigned arity) {
        MethodInfo info;
        Postcondition post;
        post.resultField = "_size";
        post.expr = SymValue::Add(SymValue::Var("old:_size"), SymValue::Const(1));
        post.usesOld = true;
        info.ensures.push_back(post);
        MethodAnnotations[{cls, method, arity}] = info;
    };

    // Helper for pop: requires _size > 0, ensures _size = old(_size) - 1
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
    // CONTAINERS WITH SIZE TRACKING
    // =========================================================================

    // std::array, std::span, std::bitset - bounds checking
    for (const char* cls : {"array", "span", "bitset"}) {
        addPre(cls, "operator[]", 1, boundsCheck());
    }
    for (const char* cls : {"array", "span"}) {
        addPre(cls, "front", 0, nonEmpty());
        addPre(cls, "back", 0, nonEmpty());
    }

    // std::vector - full tracking
    addPostConst("vector", "vector", 0, "_size", 0);
    addPostArg0("vector", "vector", 1, "_size");
    addPostArg0("vector", "vector", 2, "_size");
    addPre("vector", "operator[]", 1, boundsCheck());
    addPre("vector", "front", 0, nonEmpty());
    addPre("vector", "back", 0, nonEmpty());
    addReturns("vector", "size", "_size");
    addSizeEmpty("vector");
    addSizeIncrement("vector", "push_back", 1);
    for (unsigned i = 0; i <= 3; i++) addSizeIncrement("vector", "emplace_back", i);
    addPop("vector", "pop_back");
    addPostConst("vector", "clear", 0, "_size", 0);
    addPostArg0("vector", "resize", 1, "_size");
    addPostArg0("vector", "resize", 2, "_size");

    // std::basic_string - similar to vector
    addPostConst("basic_string", "basic_string", 0, "_size", 0);
    addPostArg0("basic_string", "basic_string", 2, "_size");
    addPre("basic_string", "operator[]", 1, boundsCheck());
    addPre("basic_string", "front", 0, nonEmpty());
    addPre("basic_string", "back", 0, nonEmpty());
    addReturns("basic_string", "size", "_size");
    addReturns("basic_string", "length", "_size");
    addSizeEmpty("basic_string");
    addSizeIncrement("basic_string", "push_back", 1);
    addPop("basic_string", "pop_back");
    addPostConst("basic_string", "clear", 0, "_size", 0);
    addPostArg0("basic_string", "resize", 1, "_size");
    addPostArg0("basic_string", "resize", 2, "_size");
    addPostArg0("basic_string", "assign", 2, "_size");  // assign(count, char)

    // std::basic_string_view - read-only view
    addPre("basic_string_view", "operator[]", 1, boundsCheck());
    addPre("basic_string_view", "front", 0, nonEmpty());
    addPre("basic_string_view", "back", 0, nonEmpty());
    addReturns("basic_string_view", "size", "_size");
    addReturns("basic_string_view", "length", "_size");
    addSizeEmpty("basic_string_view");

    // std::deque
    addPre("deque", "operator[]", 1, boundsCheck());
    addPre("deque", "front", 0, nonEmpty());
    addPre("deque", "back", 0, nonEmpty());
    addPre("deque", "pop_front", 0, nonEmpty());
    addPre("deque", "pop_back", 0, nonEmpty());
    addReturns("deque", "size", "_size");
    addSizeEmpty("deque");

    // std::list
    addPre("list", "front", 0, nonEmpty());
    addPre("list", "back", 0, nonEmpty());
    addPre("list", "pop_front", 0, nonEmpty());
    addPre("list", "pop_back", 0, nonEmpty());
    addReturns("list", "size", "_size");
    addSizeEmpty("list");

    // std::forward_list
    addPre("forward_list", "front", 0, nonEmpty());
    addPre("forward_list", "pop_front", 0, nonEmpty());
    addSizeEmpty("forward_list");

    // std::stack
    addPre("stack", "top", 0, nonEmpty());
    addPre("stack", "pop", 0, nonEmpty());
    addReturns("stack", "size", "_size");
    addSizeEmpty("stack");

    // std::queue
    addPre("queue", "front", 0, nonEmpty());
    addPre("queue", "back", 0, nonEmpty());
    addPre("queue", "pop", 0, nonEmpty());
    addReturns("queue", "size", "_size");
    addSizeEmpty("queue");

    // std::priority_queue
    addPre("priority_queue", "top", 0, nonEmpty());
    addPre("priority_queue", "pop", 0, nonEmpty());
    addReturns("priority_queue", "size", "_size");
    addSizeEmpty("priority_queue");

    // =========================================================================
    // OPTIONAL/NULLABLE TYPES
    // =========================================================================

    // std::optional
    addPostConst("optional", "optional", 0, "_has_value", 0);
    addPostConst("optional", "optional", 1, "_has_value", 1);
    addPre("optional", "value", 0, hasValue());
    addPre("optional", "operator*", 0, hasValue());
    addPre("optional", "operator->", 0, hasValue());
    addHasValueBool("optional", "has_value");
    addHasValueBool("optional", "operator bool");
    addPostConst("optional", "reset", 0, "_has_value", 0);
    for (unsigned i = 0; i <= 3; i++) addPostConst("optional", "emplace", i, "_has_value", 1);
    addPostConst("optional", "operator=", 1, "_has_value", 1);

    // std::variant
    {
        MethodInfo info;
        info.boolPostconds.push_back({"", "_has_value", ProofOp::EQ, 0, true});
        info.boolPostconds.push_back({"", "_has_value", ProofOp::EQ, 1, false});
        MethodAnnotations[{"variant", "valueless_by_exception", 0}] = info;
    }

    // std::any
    addReturns("any", "has_value", "_has_value");
    addHasValueBool("any", "operator bool");

    // =========================================================================
    // SMART POINTERS
    // =========================================================================

    // std::unique_ptr
    addPostConst("unique_ptr", "unique_ptr", 0, "_nonnull", 0);
    addPostConst("unique_ptr", "unique_ptr", 1, "_nonnull", 1);
    addPre("unique_ptr", "operator*", 0, nonnull());
    addPre("unique_ptr", "operator->", 0, nonnull());
    addNonnullBool("unique_ptr", "operator bool");
    addPostConst("unique_ptr", "reset", 0, "_nonnull", 0);
    addPostConst("unique_ptr", "reset", 1, "_nonnull", 1);
    addPostConst("unique_ptr", "release", 0, "_nonnull", 0);

    // std::shared_ptr
    addPostConst("shared_ptr", "shared_ptr", 0, "_nonnull", 0);
    addPostConst("shared_ptr", "shared_ptr", 1, "_nonnull", 1);
    addPre("shared_ptr", "operator*", 0, nonnull());
    addPre("shared_ptr", "operator->", 0, nonnull());
    addNonnullBool("shared_ptr", "operator bool");
    addPostConst("shared_ptr", "reset", 0, "_nonnull", 0);

    // std::weak_ptr
    {
        MethodInfo info;
        info.boolPostconds.push_back({"", "_nonnull", ProofOp::EQ, 0, true});
        info.boolPostconds.push_back({"", "_nonnull", ProofOp::EQ, 1, false});
        MethodAnnotations[{"weak_ptr", "expired", 0}] = info;
    }
}

} // namespace dtt
