#include "qjsp/context.hpp"
#include "qjsp/array.hpp"
#include "qjsp/class.hpp"
#include "qjsp/object.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/shape.hpp"

namespace qjsp {

Context::Context(Runtime *rt) : rt(rt) {
  rt->maybe_trigger_gc(sizeof(Context));
  ref_count   = 1;
  gc_obj_type = GCObjType::js_context;
  is_marked   = false;
  rt->add_gc_object(this);

  uint32_t count = rt->class_count;
  class_proto_count = count;
  class_protos = std::make_unique<Value[]>(count);
  for (uint32_t i = 0; i < count; ++i)
    class_protos[static_cast<size_t>(i)] = Value::null_();

  for (int i = 0; i < static_cast<int>(ErrorEnum::native_error_count); ++i)
    native_error_proto[i] = Value::undefined_();

  for (uint32_t i = static_cast<uint32_t>(ClassID::object); i < class_proto_count; ++i) {
    constexpr int kClassBase = static_cast<int>(AtomEnum::Object);
    int atom_idx             = kClassBase + static_cast<int>(i - static_cast<uint32_t>(ClassID::object));
    if (atom_idx < static_cast<int>(AtomEnum::end))
      rt->classes[i].class_name = static_cast<Atom>(atom_idx);
  }

  auto global     = Object::create(rt, Value::undefined_(), ClassID::global_object);
  global_obj = global;
  setup_global(this, global.as<Object>());
  init_array_prototype(this);
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
