#include "qjsp/context.hpp"
#include "qjsp/class.hpp"
#include "qjsp/object.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/shape.hpp"

namespace qjsp {

Context *Context::create(Runtime *rt) {
  rt->maybe_trigger_gc(sizeof(Context));
  auto *ctx = new Context();
  if (!ctx)
    return nullptr;

  ctx->ref_count   = 1;
  ctx->gc_obj_type = GCObjType::js_context;
  ctx->is_marked   = false;
  rt->add_gc_object(ctx);

  ctx->rt = rt;

  int count = static_cast<int>(rt->classes.size());
  ctx->class_protos.resize(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i)
    ctx->class_protos[static_cast<size_t>(i)] = Value::null_();

  for (int i = 0; i < static_cast<int>(ErrorEnum::native_error_count); ++i)
    ctx->native_error_proto[i] = Value::undefined_();

  for (int i = static_cast<int>(ClassID::object); i < count; ++i) {
    constexpr int kClassBase = static_cast<int>(AtomEnum::Object);
    int atom_idx             = kClassBase + (i - static_cast<int>(ClassID::object));
    if (atom_idx < static_cast<int>(AtomEnum::end))
      rt->classes[static_cast<size_t>(i)].class_name = static_cast<Atom>(atom_idx);
  }

  auto *global    = Object::create(rt, nullptr, static_cast<int>(ClassID::global_object));
  ctx->global_obj = Value::object(global);
  setup_global(ctx, global);

  return ctx;
}

void Context::destroy() {
  if (rt)
    rt->remove_gc_object(this);
  delete this;
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
  for (auto &v : class_protos) {
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
