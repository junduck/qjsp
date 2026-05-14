#include "qjsp/object.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"

namespace qjsp {

void Object::setup(Engine *e) { e->builtin_protos[static_cast<size_t>(Builtin::object)] = Object::create(e, Value::null_(), Builtin::object); }

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
  return idx < shape->size() ? properties[idx].value : Value::undefined_();
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
      properties[idx].value = value;
      return true;
    }
    if (!extensible)
      return false;
  }
  shape = e->add_shape(shape, atom, flags);
  properties.emplace_back(value);
  return true;
}

bool Object::define_own(Engine *e, Atom atom, Value value, int flags) {
  if (!shape)
    return false;
  if (auto idx = shape->find(atom); idx < shape->size()) {
    properties[idx].value = value;
    return true;
  }
  return set_own(e, atom, value, flags);
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
    if (p.value.is_object()) {
      auto *obj = p.value.as<Object>();
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
    if (p.value.is_object())
      if (auto *obj = p.value.as<Object>())
        obj->gc_refs--;
  }
}

void BytecodeFunction::gc_mark(std::vector<GCObjectHeader *> &worklist) {
  Object::gc_mark(worklist);
  for (auto &v : var_refs) {
    if (v.is_var_ref()) {
      auto *vr    = v.as<VarRef>();
      Value inner = vr->load();
      if (inner.is_object()) {
        auto *obj = inner.as<Object>();
        if (obj && !obj->is_marked) {
          obj->is_marked = true;
          worklist.push_back(obj);
        }
      }
    }
  }
}

void BytecodeFunction::gc_decref_refs() {
  Object::gc_decref_refs();
  for (auto &v : var_refs) {
    if (v.is_var_ref()) {
      auto *vr    = v.as<VarRef>();
      Value inner = vr->load();
      if (inner.is_object())
        if (auto *obj = inner.as<Object>())
          obj->gc_refs--;
    }
  }
}

void BytecodeFunction::gc_clear_refs() {
  var_refs.clear();
  Object::gc_clear_refs();
}

// ── CFunctionObj ──────────────────────────────────────────────────────────

Value CFunctionObj::create(Engine *e, CFunction *fn, std::string_view name, int length) {
  auto *obj      = new CFunctionObj();
  obj->ref_count = 1;
  obj->clsid     = Builtin::object;
  obj->fn        = fn;
  e->add_gc_object(obj);
  obj->set_own(e, e->intern("length"), Value::int32(length));
  obj->set_own(e, e->intern("name"), StrPrim::create(name));
  return Value::object(obj);
}

// ── BytecodeFunction ──────────────────────────────────────────────────────

Value BytecodeFunction::create(Engine *e, FunctionBytecode *bc) {
  auto *obj      = new BytecodeFunction();
  obj->ref_count = 1;
  obj->clsid     = Builtin::object;
  obj->bytecode  = bc;
  e->add_gc_object(obj);
  return Value::object(obj);
}

// ─── call (virtual dispatch) ───────────────────────────────────────────────

Value call(Engine *e, Value func, Value this_val, int argc, const Value *argv) {
  if (!func.is_object())
    return Value::undefined_();
  auto *obj = func.as<Object>();
  if (!obj->is_callable())
    return Value::undefined_();
  return static_cast<Callable *>(obj)->call(e, this_val, argc, argv);
}

} // namespace qjsp
