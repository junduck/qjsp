#include "qjsp/array.hpp"
#include "qjsp/class.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/object.hpp"

namespace qjsp {

Value ArrayObject::create(Engine *e) {
  e->maybe_trigger_gc(sizeof(ArrayObject));
  auto *obj        = new ArrayObject();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->clsid       = Builtin::array;
  obj->proto       = e->array_proto;
  e->add_gc_object(obj);
  return Value::object(obj);
}

bool ArrayObject::setup(Engine *e) {
  auto proto_v = Object::create(e, Value::undefined_(), Builtin::array);
  auto proto   = proto_v.as<Object>();

  {
    auto *fn        = new CFunctionObj();
    fn->ref_count   = 1;
    fn->gc_obj_type = GCObjType::js_object;
    fn->clsid       = Builtin::object;
  }
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

static Value array_iterator_next(Engine *e, Value this_val, int, const Value *) {
  auto *iter = this_val.as<Object>();
  if (!iter)
    return Value::undefined_();

  uint32_t idx = 0;
  Value idx_v  = iter->get_own(e->intern("_idx"));
  if (idx_v.is_int32())
    idx = static_cast<uint32_t>(idx_v.as_int32());

  Value arr_v = iter->get_own(e->intern("_arr"));
  if (!arr_v.is_object())
    goto done;
  {
    auto *o = arr_v.as<Object>();
    if (o->clsid != Builtin::array)
      goto done;
    auto *arr = static_cast<ArrayObject *>(o);
    if (idx >= arr->elements.size())
      goto done;
    Value r  = Object::create(e, Value::undefined_(), Builtin::object);
    auto *ro = r.as<Object>();
    ro->set_own(e, e->intern("value"), arr->elements[idx]);
    ro->set_own(e, e->intern("done"), Value::bool_(false));
    iter->set_own(e, e->intern("_idx"), Value::int32(static_cast<int32_t>(idx + 1)));
    return r;
  }
done: {
  Value r = Object::create(e, Value::undefined_(), Builtin::object);
  r.as<Object>()->set_own(e, e->intern("done"), Value::bool_(true));
  r.as<Object>()->set_own(e, e->intern("value"), Value::undefined_());
  return r;
}
}

static Value array_values(Engine *e, Value this_val, int, const Value *) {
  Value iter = Object::create(e, Value::undefined_(), Builtin::object);
  auto *o    = iter.as<Object>();
  o->set_own(e, e->intern("_arr"), this_val);
  o->set_own(e, e->intern("_idx"), Value::int32(0));
  o->set_own(e, e->intern("next"), CFunctionObj::create(e, array_iterator_next, "next", 0));
  return iter;
}

static Value array_push(Engine *e, Value this_val, int argc, const Value *argv) {
  auto *arr = this_val.as<ArrayObject>();
  if (!arr)
    return Value::undefined_();
  for (int i = 0; i < argc; i++)
    arr->elements.push_back(argv[i]);
  return Value::int32(static_cast<int32_t>(arr->elements.size()));
}

void init_array_prototype(Engine *e) {
  auto si_atom     = e->known[WellKnown::symbol_iterator];
  e->array_proto   = Object::create(e, Value::undefined_(), Builtin::object);
  auto *proto      = e->array_proto.as<Object>();
  auto si_fn       = CFunctionObj::create(e, array_values, "[Symbol.iterator]", 0);
  proto->set_own(e, si_atom, si_fn);
  auto push_fn = CFunctionObj::create(e, array_push, "push", 1);
  proto->set_own(e, e->intern("push"), push_fn);
}

} // namespace qjsp
