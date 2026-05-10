#pragma once

#include "atom.hpp"
#include "gc.hpp"
#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct Object;
struct Runtime;

enum class ClassID : uint16_t {
  object = 1,
  array,
  error,
  number,
  string,
  boolean,
  symbol,
  arguments,
  mapped_arguments,
  date,
  module_ns,
  c_function,
  bytecode_function,
  bound_function,
  c_function_data,
  generator_function,
  for_in_iterator,
  regexp,
  array_buffer,
  shared_array_buffer,
  uint8c_array,
  int8_array,
  uint8_array,
  int16_array,
  uint16_array,
  int32_array,
  uint32_array,
  big_int64_array,
  big_uint64_array,
  float16_array,
  float32_array,
  float64_array,
  dataview,
  big_int,
  map,
  set,
  weakmap,
  weakset,
  iterator,
  iterator_concat,
  iterator_helper,
  iterator_wrap,
  map_iterator,
  set_iterator,
  array_iterator,
  string_iterator,
  regexp_string_iterator,
  generator,
  global_object,
  rawjson,
  proxy,
  promise,
  promise_resolve_function,
  promise_reject_function,
  async_function,
  async_function_resolve,
  async_function_reject,
  async_from_sync_iterator,
  async_generator_function,
  async_generator,
  weak_ref,
  finalization_registry,

  init_count,
};

constexpr int kTypedArrayCount = static_cast<int>(ClassID::float64_array) - static_cast<int>(ClassID::uint8c_array) + 1;

constexpr int kCallFlagConstructor = (1 << 0);

using ClassFinalizer = void(Runtime *rt, Value val);
using ClassGCMark = void(Runtime *rt, Value val, MarkFunc *mark_func);
using ClassCall = Value(void *ctx, Value func_obj, Value this_val, int argc, const Value *argv, int flags);

struct ClassExoticMethods {
  int (*get_own_property)(void *ctx, void *desc, Value obj, Atom prop);
  int (*get_own_property_names)(void *ctx, void **ptab, uint32_t *plen, Value obj);
  int (*delete_property)(void *ctx, Value obj, Atom prop);
  int (*define_own_property)(void *ctx, Value this_obj, Atom prop, Value val, Value getter, Value setter, int flags);
  int (*has_property)(void *ctx, Value obj, Atom atom);
  Value (*get_property)(void *ctx, Value obj, Atom atom, Value receiver);
  int (*set_property)(void *ctx, Value obj, Atom atom, Value value, Value receiver, int flags);
  Value (*get_prototype)(void *ctx, Value obj);
  int (*set_prototype)(void *ctx, Value obj, Value proto_val);
  int (*is_extensible)(void *ctx, Value obj);
  int (*prevent_extensions)(void *ctx, Value obj);
};

struct ClassDef {
  const char *class_name;
  ClassFinalizer *finalizer;
  ClassGCMark *gc_mark;
  ClassCall *call;
  ClassExoticMethods *exotic;
};

struct Class {
  ClassID class_id;
  Atom class_name;
  ClassFinalizer *finalizer;
  ClassGCMark *gc_mark;
  ClassCall *call;
  const ClassExoticMethods *exotic;
};

struct ClassShortDef {
  const char *class_name;
  ClassFinalizer *finalizer;
  ClassGCMark *gc_mark;
};

enum class ErrorEnum {
  eval,
  range,
  reference,
  syntax,
  type,
  uri,
  internal,
  aggregate,
  native_error_count,
};

constexpr uint32_t kInvalidClassID = 0;

} // namespace qjsp
