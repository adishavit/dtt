// 1212_method_template_empty_fail.cpp
// Tests: Method template accessing empty vector — DTT should catch this
//
// Bug 1 fix: Pass 2b worklist must recurse into CXXRecordDecl to find
// method template specializations. Without this fix, DTT misses the
// out-of-bounds access on the empty vector inside the method template.

#include <vector>

// =============================================================================
// SECTION 1: Method template with no push_backs
// =============================================================================

// Variant 1.1: Direct access on empty vector in method template
struct BadProcessor {
    template<typename... Args>
    void run(Args... args) {
        std::vector<int> v;
        // No push_backs, just access — BUG!
        v[0];  // ERROR: _size = 0  dtt-error {{out of bounds}}
    }
};

void variant1_empty_access() {
    BadProcessor p;
    p.run(1, 2, 3);
}

// Variant 1.2: Namespaced method template with empty access
namespace ns {
    struct BadWidget {
        template<typename... Args>
        void process(Args...) {
            std::vector<int> v;
            v[0];  // ERROR: _size = 0  dtt-error {{out of bounds}}
        }
    };
}

void variant2_namespaced() {
    ns::BadWidget w;
    w.process(1);
}

int main() {
    variant1_empty_access();
    variant2_namespaced();
    return 0;
}
