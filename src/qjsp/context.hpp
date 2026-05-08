#pragma once

#include "class.hpp"
#include "gc.hpp"
#include "runtime.hpp"
#include "value.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace qjsp {

struct Shape;
struct ModuleDef;

enum class AutoInitID : uint8_t { prototype, module_ns, prop };

constexpr int kInterruptCounterInit = 10000;

struct Context : GCObjectHeader {
  Runtime *rt = nullptr;

  uint16_t binary_object_count = 0;
  int binary_object_size       = 0;

  Shape *array_shape            = nullptr;
  Shape *arguments_shape        = nullptr;
  Shape *mapped_arguments_shape = nullptr;
  Shape *regexp_shape           = nullptr;
  Shape *regexp_result_shape    = nullptr;

  std::vector<Value> class_protos;

  Value function_proto = Value::undefined_();
  Value function_ctor  = Value::undefined_();
  Value array_ctor     = Value::null_();
  Value regexp_ctor    = Value::null_();
  Value promise_ctor   = Value::null_();
  Value native_error_proto[static_cast<int>(ErrorEnum::native_error_count)]{};
  Value iterator_ctor        = Value::null_();
  Value async_iterator_proto = Value::undefined_();
  Value array_proto_values   = Value::undefined_();
  Value throw_type_error     = Value::undefined_();
  Value eval_obj             = Value::undefined_();

  Value global_obj     = Value::undefined_();
  Value global_var_obj = Value::undefined_();

  uint64_t random_state = 0;
  int interrupt_counter = kInterruptCounterInit;

  Value (*compile_regexp)(void *ctx, Value pattern, Value flags)                                                                         = nullptr;
  Value (*eval_internal)(void *ctx, Value this_obj, const char *input, size_t input_len, const char *filename, int flags, int scope_idx) = nullptr;
  void *user_opaque                                                                                                                      = nullptr;

  static Context *create(Runtime *rt);
  void destroy();

  // ── GC ───────────────────────────────────────────────────────────────
  void gc_mark(std::vector<GCObjectHeader *> &worklist);

private:
  Context() = default;
};

} // namespace qjsp
