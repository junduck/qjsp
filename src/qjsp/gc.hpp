#pragma once

#include <cstdint>
#include <vector>

namespace qjsp {

enum class GCPhase : uint8_t { none, decref, remove_cycles };

enum class GCObjType : uint8_t {
  js_object,
  function_bytecode,
  shape,
  var_ref,
  async_function,
  js_context,
  module,
};

enum class WeakRefType : uint8_t { map, weakref, finrec };

struct RefCounted {
  int ref_count = 1;
  void ref() { ++ref_count; }
  bool unref() { return --ref_count == 0; }
};

struct GCObjectHeader : RefCounted {
  GCObjType gc_obj_type = GCObjType::js_object;
  bool is_marked        = false;

  virtual ~GCObjectHeader() = default;
};

using GCObjList = std::vector<GCObjectHeader *>;

struct WeakRefHeader {
  WeakRefType weakref_type{};
};

using MarkFunc = void(void *, GCObjectHeader *);

} // namespace qjsp
