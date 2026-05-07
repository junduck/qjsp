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
  int binary_object_size = 0;

  Shape *array_shape = nullptr;
  Shape *arguments_shape = nullptr;
  Shape *mapped_arguments_shape = nullptr;
  Shape *regexp_shape = nullptr;
  Shape *regexp_result_shape = nullptr;

  std::vector<Value> class_protos;

  Value function_proto = kUndefined;
  Value function_ctor = kUndefined;
  Value array_ctor = kNull;
  Value regexp_ctor = kNull;
  Value promise_ctor = kNull;
  Value native_error_proto[static_cast<int>(ErrorEnum::native_error_count)]{};
  Value iterator_ctor = kNull;
  Value async_iterator_proto = kUndefined;
  Value array_proto_values = kUndefined;
  Value throw_type_error = kUndefined;
  Value eval_obj = kUndefined;

  Value global_obj = kUndefined;
  Value global_var_obj = kUndefined;

  uint64_t random_state = 0;
  int interrupt_counter = kInterruptCounterInit;

  Value (*compile_regexp)(void *ctx, Value pattern, Value flags) = nullptr;
  Value (*eval_internal)(void *ctx, Value this_obj, const char *input, size_t input_len, const char *filename, int flags, int scope_idx) = nullptr;
  void *user_opaque = nullptr;

  static Context *create(Runtime *rt);
  void destroy();

  // ── GC ───────────────────────────────────────────────────────────────
  void gc_mark(std::vector<GCObjectHeader*>& worklist);

private:
  Context() = default;
};

} // namespace qjsp
