// XFAIL: Void function heuristic limitations — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// The void function heuristic assumes void functions with non-const ref
// parameters might mutate them (invalidating proofs). This is often too
// conservative for observer/query functions that happen to return void.
//
// WHY IT'S DIFFICULT:
// 1. Can't distinguish "modifies" from "observes" without deeper analysis
// 2. Would need escape analysis or effect system
// 3. Annotation burden: requiring [[dtt:pure]] on every query function
// 4. Template functions: same code might be pure or impure depending on T
//
// RATIONALE:
// Many legitimate void functions don't modify their arguments:
// - print(const std::string&) - but what about print(std::ostream&, ...)?
// - validate(const Data&) - observation only
// - forEach(Container&, Callback) - callback might not modify
//
// CURRENT BEHAVIOR:
// Void functions without DTT annotations taking non-const ref/pointer
// invalidate all properties of passed arguments. This is safe but
// causes false positives when query functions happen to return void.

struct Data {
    int* values;
    int _count;
};

// This function only READS data, despite taking non-const ref
// (Maybe it needs non-const for internal caching, or legacy API)
void printData(Data& d);  // No DTT annotation, returns void

[[clang::annotate("dtt:requires=_count > 0")]]
void processData(Data& d);

int main() {
    Data d;
    d._count = 10;  // Known: d._count = 10

    printData(d);  // Void + non-const ref = might mutate (heuristic)

    // XFAIL: d._count invalidated by void function heuristic
    processData(d);  // Should PASS if printData is pure

    // WORKAROUND: User must add [[clang::annotate("dtt:pure")]] to printData
    // or use [[clang::annotate("dtt:ensures=_count == @old(_count)")]]

    return 0;
}
