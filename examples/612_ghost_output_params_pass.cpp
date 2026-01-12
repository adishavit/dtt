// Example 612: Ghost state for output parameters
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a function has output pointer parameters and all callers pass
// addresses of local variables (always non-null), the parameters should
// be proven non-null via ghost state analysis.
//
// This is a regression test for the tinyexr ReadAttribute pattern.
//
// Expected: PASS (all callers pass non-null for output params)

#include <string>
#include <vector>

namespace tinyexr {

static bool ReadAttribute(std::string *name, std::string *type,
                          std::vector<unsigned char> *data, size_t *marker_size,
                          const char *marker, size_t size) {
    if (size == 0) {
        return false;
    }

    *name = std::string("test");    // PASS: all callers pass non-null
    *type = std::string("string");  // PASS: all callers pass non-null

    *marker_size = 10;              // PASS: all callers pass non-null
    data->resize(10);               // PASS: all callers pass non-null

    return true;
}

void caller1() {
    std::string attr_name, attr_type;
    std::vector<unsigned char> data;
    size_t marker_size;
    ReadAttribute(&attr_name, &attr_type, &data, &marker_size, "marker", 10);
}

void caller2() {
    std::string attr_name, attr_type;
    std::vector<unsigned char> data;
    size_t marker_size;
    ReadAttribute(&attr_name, &attr_type, &data, &marker_size, "other", 5);
}

}  // namespace tinyexr

int main() {
    tinyexr::caller1();
    tinyexr::caller2();
    return 0;
}
