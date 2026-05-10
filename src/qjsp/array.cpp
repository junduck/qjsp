#include "qjsp/array.hpp"
#include "qjsp/class.hpp"
#include "qjsp/context.hpp"
#include "qjsp/runtime.hpp"

namespace qjsp {

Value ArrayObject::create(Runtime *rt, Value proto) {
  rt->maybe_trigger_gc(sizeof(ArrayObject));
  auto *obj        = new ArrayObject();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->class_id    = ClassID::array;
  obj->proto       = proto;
  rt->add_gc_object(obj);
  return Value::object(obj);
}

void ArrayObject::gc_mark(std::vector<GCObjectHeader *> &worklist) {
  Object::gc_mark(worklist);
  for (auto &v : elements) {
    if (v.is_object()) {
      auto *obj = v.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  }
}

// ── Array.prototype[Symbol.iterator] ───────────────────────────────────────

static Value array_iterator_next(Context *ctx, Value this_val, int, const Value *) {
  auto *rt   = ctx->rt;
  auto *iter = this_val.as<Object>();
  if (!iter)
    return Value::undefined_();

  uint32_t idx = 0;
  Value idx_v  = iter->get_own(rt->intern("_idx"));
  if (idx_v.is_int32())
    idx = static_cast<uint32_t>(idx_v.as_int32());

  Value arr_v = iter->get_own(rt->intern("_arr"));
  if (!arr_v.is_object())
    goto done;
  {
    auto *o = arr_v.as<Object>();
    if (o->class_id != ClassID::array)
      goto done;
    auto *arr = static_cast<ArrayObject *>(o);
    if (idx >= arr->elements.size())
      goto done;
    Value r  = Object::create(rt, Value::undefined_(), ClassID::object);
    auto *ro = r.as<Object>();
    ro->set_own(rt, rt->intern("value"), arr->elements[idx]);
    ro->set_own(rt, rt->intern("done"), Value::bool_(false));
    iter->set_own(rt, rt->intern("_idx"), Value::int32(static_cast<int32_t>(idx + 1)));
    return r;
  }
done: {
  Value r = Object::create(rt, Value::undefined_(), ClassID::object);
  r.as<Object>()->set_own(rt, rt->intern("done"), Value::bool_(true));
  r.as<Object>()->set_own(rt, rt->intern("value"), Value::undefined_());
  return r;
}
}

static Value array_values(Context *ctx, Value this_val, int, const Value *) {
  auto *rt   = ctx->rt;
  Value iter = Object::create(rt, Value::undefined_(), ClassID::object);
  auto *o    = iter.as<Object>();
  o->set_own(rt, rt->intern("_arr"), this_val);
  o->set_own(rt, rt->intern("_idx"), Value::int32(0));
  o->set_own(rt, rt->intern("next"), CFunctionObj::create(ctx, array_iterator_next, "next", 0));
  return iter;
}

void init_array_prototype(Context *ctx) {
  auto *rt         = ctx->rt;
  ctx->array_proto = Object::create(rt, Value::undefined_(), ClassID::object);
  auto *proto      = ctx->array_proto.as<Object>();
  auto si_fn       = CFunctionObj::create(ctx, array_values, "[Symbol.iterator]", 0);
  proto->set_own(rt, rt->well_known.symbol_iterator, si_fn);
}

} // namespace qjsp
