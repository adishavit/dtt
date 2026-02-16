// DTTStdLib.cpp - STL type annotations implementation for DTT
//
// Annotations are defined declaratively in annotations/std.dtt and embedded
// here as a raw string literal. The parser in DTTAnnotationFile.cpp converts
// them into the same MethodAnnotations/TemplateAliases maps that the old
// hand-written C++ registration code produced.

#include "DTTStdLib.h"
#include "DTTCLib.h"
#include "DTTAnnotationFile.h"

namespace dtt {

llvm::DenseMap<MethodKey, MethodInfo> MethodAnnotations;
llvm::StringMap<llvm::SmallVector<TemplateParamAlias, 2>> TemplateAliases;

// Embedded STL annotations — edit annotations/std.dtt for the readable source.
static constexpr const char kStdAnnotations[] = R"DTT(
# STL Type Annotations for DTT (embedded copy of annotations/std.dtt)

template std::array {
  param 1 => _size
}

template std::span {
  param 1 => _size skip -1
}

template std::bitset {
  param 0 => _size
}

# Reusable traits

trait SizedContainer {
  @ctor(0):
    ensures _size = 0
  empty(0):
    ensures_if_true _size == 0
    ensures_if_false _size == 1
  clear(0):
    ensures _size = 0
  size(0):
    returns _size
}

trait UnsizedContainer {
  @ctor(0):
    ensures _size = 0
  empty(0):
    ensures_if_true _size == 0
    ensures_if_false _size == 1
  clear(0):
    ensures _size = 0
}

trait SequenceAccess {
  operator[](1):
    requires arg0 < _size
  at(1):
    requires arg0 < _size
  front(0):
    requires _size > 0
  back(0):
    requires _size > 0
}

trait BoundsCheck {
  operator[](1):
    requires arg0 < _size
}

trait FrontAccess {
  front(0):
    requires _size > 0
}

trait FrontBackAccess {
  front(0):
    requires _size > 0
  back(0):
    requires _size > 0
}

trait TopAccess {
  top(0):
    requires _size > 0
}

trait PushBack {
  push_back(1):
    ensures _size = old(_size) + 1
  emplace_back(0..3):
    ensures _size = old(_size) + 1
  pop_back(0):
    requires _size > 0
    ensures _size = old(_size) - 1
}

trait PushFront {
  push_front(1):
    ensures _size = old(_size) + 1
  emplace_front(0..3):
    ensures _size = old(_size) + 1
  pop_front(0):
    requires _size > 0
    ensures _size = old(_size) - 1
}

trait SequenceMutate {
  insert(2):
    ensures _size = old(_size) + 1
  insert(3):
    ensures _size = old(_size) + arg1
  emplace(1..4):
    ensures _size = old(_size) + 1
  erase(1):
    requires _size > 0
    ensures _size = old(_size) - 1
  resize(1):
    ensures _size = arg0
  resize(2):
    ensures _size = arg0
  assign(2):
    ensures _size = arg0
}

trait IteratorPreserving {
  begin(0):
    ensures _size = old(_size)
  end(0):
    ensures _size = old(_size)
  rbegin(0):
    ensures _size = old(_size)
  rend(0):
    ensures _size = old(_size)
}

trait AdapterPush {
  push(1):
    ensures _size = old(_size) + 1
  emplace(0..3):
    ensures _size = old(_size) + 1
  pop(0):
    requires _size > 0
    ensures _size = old(_size) - 1
}

trait SetInsert {
  insert(1):
    ensures _size = old(_size) + 1
}

# Fixed-size containers

type array : BoundsCheck {
  at(1):
    requires arg0 < _size
  front(0):
    requires _size > 0
  back(0):
    requires _size > 0
  data(0):
    ensures _size = old(_size)
}

type span : BoundsCheck {
  front(0):
    requires _size > 0
  back(0):
    requires _size > 0
  span(2):
    ensures _size = arg1
}

type bitset : BoundsCheck {
}

# Sequence containers

type vector : SizedContainer, SequenceAccess, PushBack, SequenceMutate, IteratorPreserving {
  vector(1):
    ensures _size = arg0
  vector(2):
    ensures _size = arg0
  vector(3):
    ensures _size = arg0
  data(0):
    ensures _size = old(_size)
}

type deque : SizedContainer, SequenceAccess, PushBack, PushFront, SequenceMutate, IteratorPreserving {
}

type list : SizedContainer, FrontBackAccess, PushBack, PushFront, SequenceMutate, IteratorPreserving {
}

type forward_list : UnsizedContainer, FrontAccess, PushFront {
}

# Ordered associative containers

type set : SizedContainer, SetInsert {
}

type multiset : SizedContainer, SetInsert {
}

type map : SizedContainer {
  at(1):
    requires _size > 0
}

type multimap : SizedContainer {
}

# Unordered associative containers

type unordered_set : SizedContainer, SetInsert {
}

type unordered_multiset : SizedContainer, SetInsert {
}

type unordered_map : SizedContainer {
  at(1):
    requires _size > 0
}

type unordered_multimap : SizedContainer {
}

# Container adapters

type stack : SizedContainer, TopAccess, AdapterPush {
}

type queue : SizedContainer, FrontBackAccess, AdapterPush {
}

type priority_queue : SizedContainer, TopAccess, AdapterPush {
}

# std::basic_string

type basic_string : IteratorPreserving {
  basic_string(0):
    ensures _size = 0
  empty(0):
    ensures_if_true _size == 0
    ensures_if_false _size == 1
  size(0):
    returns _size
  length(0):
    returns _size
  push_back(1):
    ensures _size = old(_size) + 1
  operator+=(1):
    ensures _size = old(_size) + 1
  pop_back(0):
    requires _size > 0
    ensures _size = old(_size) - 1
  clear(0):
    ensures _size = 0
  resize(1):
    ensures _size = arg0
  resize(2):
    ensures _size = arg0
  assign(2):
    ensures _size = arg0
  append(2):
    ensures _size = old(_size) + arg0
  operator[](1):
    requires arg0 < _size
  at(1):
    requires arg0 < _size
  front(0):
    requires _size > 0
  back(0):
    requires _size > 0
  data(0):
    ensures _size = old(_size)
  c_str(0):
    ensures _size = old(_size)
}

# std::basic_string_view

type basic_string_view {
  basic_string_view(0):
    ensures _size = 0
  basic_string_view(2):
    ensures _size = arg1
  operator[](1):
    requires arg0 < _size
  at(1):
    requires arg0 < _size
  front(0):
    requires _size > 0
  back(0):
    requires _size > 0
  size(0):
    returns _size
  length(0):
    returns _size
  empty(0):
    ensures_if_true _size == 0
    ensures_if_false _size == 1
  remove_prefix(1):
    ensures _size = old(_size) - arg0
  remove_suffix(1):
    ensures _size = old(_size) - arg0
  data(0):
    ensures _size = old(_size)
}

# std::optional

type optional {
  optional(0):
    ensures _has_value = 0
  optional(1):
    ensures _has_value = 1
  value(0):
    requires _has_value == 1
  operator*(0):
    requires _has_value == 1
  operator->(0):
    requires _has_value == 1
  has_value(0):
    ensures_if_true _has_value == 1
    ensures_if_false _has_value == 0
  operator bool(0):
    ensures_if_true _has_value == 1
    ensures_if_false _has_value == 0
  reset(0):
    ensures _has_value = 0
  emplace(0..3):
    ensures _has_value = 1
  operator=(1):
    ensures _has_value = 1
}

# std::variant

type variant {
  variant(0):
    ensures _variant_index = 0
  valueless_by_exception(0):
    ensures_if_true _has_value == 0
    ensures_if_false _has_value == 1
  index(0):
    returns _variant_index
}

# std::any

type any {
  any(0):
    ensures _has_value = 0
  any(1):
    ensures _has_value = 1
  has_value(0):
    ensures_if_true _has_value == 1
    ensures_if_false _has_value == 0
  reset(0):
    ensures _has_value = 0
  operator=(1):
    ensures _has_value = 1
  emplace(0..3):
    ensures _has_value = 1
}

# std::expected (C++23)

type expected {
  expected(0):
    ensures _has_value = 1
  expected(1):
    ensures _has_value = 1
  value(0):
    requires _has_value == 1
  operator*(0):
    requires _has_value == 1
  operator->(0):
    requires _has_value == 1
  error(0):
    requires _has_value == 0
  has_value(0):
    ensures_if_true _has_value == 1
    ensures_if_false _has_value == 0
  operator bool(0):
    ensures_if_true _has_value == 1
    ensures_if_false _has_value == 0
  emplace(0..3):
    ensures _has_value = 1
}

# Smart pointers

type unique_ptr {
  unique_ptr(0):
    ensures _nonnull = 0
  unique_ptr(1):
    ensures _nonnull = 1
  operator*(0):
    requires _nonnull == 1
  operator->(0):
    requires _nonnull == 1
  operator bool(0):
    ensures_if_true _nonnull == 1
    ensures_if_false _nonnull == 0
  reset(0):
    ensures _nonnull = 0
  reset(1):
    ensures _nonnull = 1
  release(0):
    ensures _nonnull = 0
}

type shared_ptr {
  shared_ptr(0):
    ensures _nonnull = 0
  shared_ptr(1):
    ensures _nonnull = 1
  operator*(0):
    requires _nonnull == 1
  operator->(0):
    requires _nonnull == 1
  operator bool(0):
    ensures_if_true _nonnull == 1
    ensures_if_false _nonnull == 0
  reset(0):
    ensures _nonnull = 0
  reset(1):
    ensures _nonnull = 1
}

type weak_ptr {
  expired(0):
    ensures_if_true _nonnull == 0
    ensures_if_false _nonnull == 1
}
)DTT";

void registerSyntheticAnnotations() {
    parseAnnotations(kStdAnnotations, MethodAnnotations, FreeFunctionAnnotations,
                     TemplateAliases);
}

} // namespace dtt
