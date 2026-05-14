#include "qjsp/context.hpp"
#include "qjsp/class.hpp"
#include "qjsp/object.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/shape.hpp"

namespace qjsp {

Context::Context(Runtime *rt) : rt(rt) {
  rt->maybe_trigger_gc(sizeof(Context));
  ref_count = 1;
  is_marked = false;
  rt->add_gc_object(this);

  uint32_t count    = rt->class_count;
  class_proto_count = count;
  class_protos      = std::make_unique<Value[]>(count);
  for (uint32_t i = 0; i < count; ++i)
    class_protos[static_cast<size_t>(i)] = Value::null_();

  for (int i = 0; i < static_cast<int>(ErrorEnum::native_error_count); ++i)
    native_error_proto[i] = Value::undefined_();

  for (uint32_t i = static_cast<uint32_t>(ClassID::object); i < class_proto_count; ++i) {
    rt->classes[i].class_name = kAtomNull;
  }
}

Context::~Context() {
  if (rt)
    rt->remove_gc_object(this);
}

void Context::gc_mark(std::vector<GCObjectHeader *> &worklist) {
  is_marked = true;
  if (global_obj.is_object()) {
    auto *obj = global_obj.as<Object>();
    if (obj && !obj->is_marked) {
      obj->is_marked = true;
      worklist.push_back(obj);
    }
  }
  if (array_proto.is_object()) {
    auto *obj = array_proto.as<Object>();
    if (obj && !obj->is_marked) {
      obj->is_marked = true;
      worklist.push_back(obj);
    }
  }
  for (uint32_t i = 0; i < class_proto_count; ++i) {
    auto &v = class_protos[i];
    if (v.is_object()) {
      auto *obj = v.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  }
}

} // namespace qjsp
