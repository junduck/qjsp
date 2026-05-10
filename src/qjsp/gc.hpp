#pragma once

#include <cstdint>
#include <vector>

namespace qjsp {

enum class GCPhase : uint8_t { none, decref, remove_cycles };

enum class GCObjType : uint8_t {
  js_object,
  shape,
  async_function,
  js_context,
  module,
};

struct RefCounted {
  int ref_count = 1;
  void ref() { ++ref_count; }
  bool unref() { return --ref_count == 0; }
};

struct GCObjectHeader : RefCounted {
  GCObjType gc_obj_type = GCObjType::js_object;
  bool is_marked        = false;

  virtual void gc_mark(std::vector<GCObjectHeader *> &worklist) = 0;
  virtual ~GCObjectHeader()                                     = default;
};

using GCObjList = std::vector<GCObjectHeader *>;

} // namespace qjsp
