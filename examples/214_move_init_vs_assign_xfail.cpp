// XFAIL: Move initialization vs assignment — different behaviors
//
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// This demonstrates the gap between move ASSIGNMENT and move INITIALIZATION:
//
// Move ASSIGNMENT (works):
//   Buffer b;
//   b = std::move(a);  // Properties copied, source invalidated
//
// Move INITIALIZATION (broken):
//   Buffer b = std::move(a);  // Properties NOT copied, source NOT invalidated
//
// ============================================================================
// WHY THE DIFFERENCE
// ============================================================================
// Move ASSIGNMENT goes through VisitBinaryOperator or VisitCXXOperatorCallExpr
// which has explicit isStdMove() handling:
//
//   if (isStdMove(RHS)) {
//       copyProperties(lhsPath, rhsPath);
//       invalidateProperties(rhsPath);
//   }
//
// Move INITIALIZATION goes through VisitVarDecl -> CXXConstructExpr
// which only handles DeclRefExpr args, not CallExpr (std::move):
//
//   if (auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
//       copyProperties(var, srcPath);
//   }
//   // No isStdMove check here!
//
// ============================================================================
// FIX REQUIRED
// ============================================================================
// In VisitVarDecl's CXXConstructExpr handling, add:
//
//   if (isStdMove(Arg)) {
//       std::string srcPath = getMovedFromPath(Arg);
//       copyProperties(var, srcPath);
//       invalidateProperties(srcPath);
//   }
//
// ============================================================================

#include <utility>

struct Buffer {
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Buffer& b);

int main() {
    // CASE 1: Move ASSIGNMENT — properties transfer (WORKS)
    Buffer a1;
    a1._size = 10;
    Buffer b1;
    b1 = std::move(a1);     // a1 invalidated, b1._size = 10
    needsNonEmpty(b1);      // PASSES: b1._size known to be 10

    // CASE 2: Move INITIALIZATION — properties DON'T transfer (XFAIL)
    Buffer a2;
    a2._size = 20;
    Buffer b2 = std::move(a2);  // Properties NOT copied!

    // XFAIL: b2._size is unknown (should be 20)
    needsNonEmpty(b2);      // FAILS: b2._size unknown

    return 0;
}
