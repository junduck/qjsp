#pragma once

#include "atom.hpp"
#include <cstdint>

namespace qjsp {

enum class Builtin : uint16_t {
  object = 0,
  array,

  // Sentinel
  BuiltinCount
};

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

struct Class {
  ClassID class_id;
  Atom class_name;
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
