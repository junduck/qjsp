#include "qjsp/object.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/function.hpp"
#include "qjsp/string.hpp"

namespace qjsp {

// ─── Object builtin ──────────────────────────────────────────────────────────
//
//  Follows the same 5‑stage pattern documented in array.cpp::setup().
//  Object is the root of the prototype hierarchy: Object.prototype has
//  null __proto__, and all other builtins chain their prototype to it.
//
//  Object constructor:
//    `new Object()` — returns this_val (the pre‑created plain object).
//    `Object()`     — creates and returns a new plain object.

namespace {
Value object_constructor(Engine *e, Value this_val, int, const Value *) {
  if (this_val.is_object() && this_val.as<Object>()->clsid == Builtin::object)
    return this_val;
  return Object::create(e, e->get_proto(Builtin::object), Builtin::object);
}
} // namespace

void Object::setup(Engine *e) {
  constexpr auto id = Builtin::object;
  auto idx          = static_cast<size_t>(id);

  // Stage 1: Prototype — null __proto__ (root of the chain)
  Value proto            = Object::create(e, Value::null_(), id);
  e->builtin_protos[idx] = proto;

  // Stage 2: Methods — none yet (toString, valueOf, … go here)

  // Stage 3: Constructor
  Value ctor = CFunctionObj::create(e, object_constructor, "Object", 1);

  // Stage 4: Linkage
  auto proto_atom = e->intern("prototype");
  auto cons_atom  = e->intern("constructor");
  ctor.as<Object>()->set_own(e, proto_atom, proto);
  proto.as<Object>()->set_own(e, cons_atom, ctor);

  // Stage 5: Export
  e->global_obj.as<Object>()->set_own(e, e->intern("Object"), ctor);
}

Value Object::create(Engine *e, Value proto, Builtin clsid) {
  auto *obj       = new Object();
  obj->ref_count  = 1;
  obj->extensible = true;
  obj->clsid      = clsid;
  obj->proto      = proto;
  e->add_gc_object(obj);
  return Value::object(obj);
}

// ── property access ────────────────────────────────────────────────────────

Value Object::get_own(Engine *e, Atom atom) {
  if (!shape)
    return Value::undefined_();
  auto idx = shape->find(atom);
  if (idx >= shape->size())
    return Value::undefined_();
  auto &prop  = properties[idx];
  int flags   = shape->entries[idx].flags;
  if ((flags & kPropGetter) && prop.getter.is_callable())
    return call(e, prop.getter, Value::object(this), 0, nullptr);
  return prop.value;
}

Value Object::get(Engine *e, Atom atom) {
  for (auto *cur = this; cur; cur = cur->proto.is_object() ? cur->proto.as<Object>() : nullptr) {
    Value v = cur->get_own(e, atom);
    if (!v.is_undefined())
      return v;
  }
  return Value::undefined_();
}

bool Object::set_own(Engine *e, Atom atom, Value value, int flags) {
  if (shape) {
    auto idx = shape->find(atom);
    if (idx < shape->size()) {
      auto &prop         = properties[idx];
      int existing_flags = shape->entries[idx].flags;
      // accessor: dispatch through setter
      if ((existing_flags & kPropSetter) && prop.setter.is_callable()) {
        const Value args[] = {value};
        call(e, prop.setter, Value::object(this), 1, args);
        return true;
      }
      // data property: refuse if not writable
      if (!(existing_flags & kPropWritable))
        return false;
      prop.value = value;
      return true;
    }
    if (!extensible)
      return false;
  }
  shape = e->add_shape(shape, atom, flags);
  properties.push_back({value, Value::undefined_(), Value::undefined_()});
  return true;
}

// ── GC ─────────────────────────────────────────────────────────────────────

void Object::gc_mark(std::vector<GCObjectHeader *> &worklist) {
  is_marked = true;
  auto mark_val = [&](Value &v) {
    if (v.is_object()) {
      auto *obj = v.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  };
  mark_val(proto);
  for (auto &p : properties) {
    mark_val(p.value);
    mark_val(p.getter);
    mark_val(p.setter);
  }
}

void Object::gc_clear_refs() {
  proto = Value::undefined_();
  properties.clear();
}

void Object::gc_decref_refs() {
  auto dec = [](Value &v) {
    if (v.is_object())
      if (auto *obj = v.as<Object>())
        obj->gc_refs--;
  };
  dec(proto);
  for (auto &p : properties) {
    dec(p.value);
    dec(p.getter);
    dec(p.setter);
  }
}

} // namespace qjsp
