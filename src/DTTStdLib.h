// DTTStdLib.h - STL type annotations for DTT
//
// Provides hardcoded semantic annotations for standard library types:
// - std::array, std::span, std::bitset (bounds checking)
// - std::vector (size tracking, bounds checking)
// - std::basic_string/std::string (size tracking, bounds checking)
// - std::string_view (size tracking, bounds checking)
// - std::optional (has_value tracking)
// - std::unique_ptr, std::shared_ptr, std::weak_ptr (nullability tracking)
// - std::deque, std::list, std::forward_list (size tracking)
// - std::stack, std::queue, std::priority_queue (size tracking)
// - std::variant, std::any (has_value tracking)
// - std::expected (has_value tracking, C++23)

#ifndef DTT_STDLIB_H
#define DTT_STDLIB_H

#include "DTTTypes.h"

namespace dtt {

// Global map of method annotations
extern llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;

// Template parameter to property mappings (e.g., std::array<T,N> → _size = N)
extern std::map<std::string, std::vector<TemplateParamAlias>> TemplateAliases;

// Register all STL type annotations into MethodAnnotations
void registerSyntheticAnnotations();

} // namespace dtt

#endif // DTT_STDLIB_H
