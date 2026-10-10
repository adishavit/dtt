// DTTAnnotationFile.h - .dtt annotation file parser and built-in annotations
//
// Parses declarative annotation files (.dtt format) and manages the global
// annotation maps used by the DTT plugin. Built-in annotations (std, libc,
// win32) are embedded in the plugin, with optional plugin-relative overrides.
// Users can supply additional .dtt files via --dtt-annotations= plugin arg.

#ifndef DTT_ANNOTATION_FILE_H
#define DTT_ANNOTATION_FILE_H

#include "DTTTypes.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

namespace dtt {

// Global annotation maps populated by registerBuiltinAnnotations()
extern llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;
extern llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo> FreeFunctionAnnotations;
extern llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>> TemplateAliases;

/// Load built-in .dtt annotation files (std.dtt, libc.dtt, win32.dtt)
/// from plugin-relative overrides or embedded defaults. Returns false on errors.
bool registerBuiltinAnnotations();

/// Parse .dtt format annotation string into existing maps.
/// Returns true on success, false on parse error.
/// Errors are reported to llvm::errs().
bool parseAnnotations(
    llvm::StringRef content,
    llvm::DenseMap<MethodKey, MethodInfo>& methodAnnotations,
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo>& freeFuncAnnotations,
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>>& templateAliases);

/// Parse .dtt file from disk.
/// Returns true on success, false on file read or parse error.
bool parseAnnotationFile(
    llvm::StringRef path,
    llvm::DenseMap<MethodKey, MethodInfo>& methodAnnotations,
    llvm::DenseMap<FreeFunctionKey, FreeFunctionInfo>& freeFuncAnnotations,
    llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>>& templateAliases);

} // namespace dtt

#endif // DTT_ANNOTATION_FILE_H
