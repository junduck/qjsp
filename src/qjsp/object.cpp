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

Value Object::get_own(Atom atom) const {
  if (!shape)
    return Value::undefined_();
  auto idx = shape->find(atom);
  return idx < shape->size() ? properties[idx] : Value::undefined_();
}

Value Object::get(Atom atom) const {
  for (auto *cur = this; cur; cur = cur->proto.is_object() ? cur->proto.as<Object>() : nullptr) {
    if (Value v = cur->get_own(atom); !v.is_undefined())
      return v;
  }
  return Value::undefined_();
}

bool Object::set_own(Engine *e, Atom atom, Value value, int flags) {
  if (shape) {
    if (auto idx = shape->find(atom); idx < shape->size()) {
      properties[idx] = value;
      return true;
    }
    if (!extensible)
      return false;
  }
  shape = e->add_shape(shape, atom, flags);
  properties.emplace_back(value);
  return true;
}

void Object::gc_mark(std::vector<GCObjectHeader *> &worklist) {
  is_marked = true;
  if (proto.is_object()) {
    auto *p = proto.as<Object>();
    if (p && !p->is_marked) {
      p->is_marked = true;
      worklist.push_back(p);
    }
  }
  for (auto &p : properties) {
    if (p.is_object()) {
      auto *obj = p.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  }
}

void Object::gc_clear_refs() {
  proto = Value::undefined_();
  properties.clear();
}

void Object::gc_decref_refs() {
  if (proto.is_object())
    if (auto *p = proto.as<Object>())
      p->gc_refs--;
  for (auto &p : properties) {
    if (p.is_object())
      if (auto *obj = p.as<Object>())
        obj->gc_refs--;
  }
}

} // namespace qjsp
