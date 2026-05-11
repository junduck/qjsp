#include "qjsp/object.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/context.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"
#include <cstdio>

namespace qjsp {

Value Object::create(Runtime *rt, Value proto, ClassID class_id) {
  rt->maybe_trigger_gc(sizeof(Object));
  auto *obj        = new Object();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->extensible  = true;
  obj->class_id    = class_id;
  obj->proto       = proto;
  rt->add_gc_object(obj);
  return Value::object(obj);
}

// ── CFunctionObj ──────────────────────────────────────────────────────────

Value CFunctionObj::create(Context *ctx, CFunction *fn, std::string_view name, int length) {
  auto *obj        = new CFunctionObj();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->class_id    = ClassID::c_function;
  obj->fn = fn;
  ctx->rt->add_gc_object(obj);
  obj->set_own(ctx->rt, ctx->rt->intern("length"), Value::int32(length));
  obj->set_own(ctx->rt, ctx->rt->intern("name"), String::create(name));
  return Value::object(obj);
}

// ── BytecodeFunction ──────────────────────────────────────────────────────

Value BytecodeFunction::create(Runtime *rt, FunctionBytecode *bc) {
  auto *obj        = new BytecodeFunction();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->class_id    = ClassID::bytecode_function;
  obj->bytecode    = bc;
  rt->add_gc_object(obj);
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
    Value v = cur->get_own(atom);
    if (!v.is_undefined())
      return v;
  }
  return Value::undefined_();
}

bool Object::set_own(Runtime *rt, Atom atom, Value value, int flags) {
  if (shape) {
    auto idx = shape->find(atom);
    if (idx < shape->size()) {
      properties[idx].value = value;
      return true;
    }
    if (!extensible)
      return false;
  }
  shape = rt->add_shape(shape, atom, flags);
  properties.emplace_back(value);
  return true;
}

bool Object::define_own(Runtime *rt, Atom atom, Value value, int flags) {
  if (!shape)
    return false;
  auto idx = shape->find(atom);
  if (idx < shape->size()) {
    properties[idx].value = value;
    return true;
  }
  return set_own(rt, atom, value, flags);
}

void Object::destroy(Runtime *rt) {
  rt->remove_gc_object(this);
  delete this;
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

void BytecodeFunction::gc_mark(std::vector<GCObjectHeader *> &worklist) { Object::gc_mark(worklist); }

// ─── call (virtual dispatch) ───────────────────────────────────────────────

Value call(Context *ctx, Value func, Value this_val, int argc, const Value *argv) {
  if (!func.is_object())
    return Value::undefined_();
  auto *obj = func.as<Object>();
  if (!obj->is_callable())
    return Value::undefined_();
  return static_cast<Callable *>(obj)->call(ctx, this_val, argc, argv);
}

// ─── built-in: print ───────────────────────────────────────────────────────

static Value builtin_print(Context * /*ctx*/, Value /*this_val*/, int argc, const Value *argv) {
  for (int i = 0; i < argc; ++i) {
    if (i > 0)
      std::putchar(' ');
    if (argv[i].is_string()) {
      auto *s = argv[i].as<String>();
      std::fwrite(s->data, 1, s->len(), stdout);
    } else if (argv[i].is_int32()) {
      std::fprintf(stdout, "%d", argv[i].as_int32());
    } else if (argv[i].is_double()) {
      std::fprintf(stdout, "%g", argv[i].as_double());
    } else if (argv[i].is_bool()) {
      std::fputs(argv[i].as_bool() ? "true" : "false", stdout);
    } else if (argv[i].is_null()) {
      std::fputs("null", stdout);
    } else if (argv[i].is_undefined()) {
      std::fputs("undefined", stdout);
    }
  }
  std::putchar('\n');
  return Value::undefined_();
}

void setup_global(Context *ctx, Object *global) {
  auto *rt = ctx->rt;

  // print
  auto fn = CFunctionObj::create(ctx, builtin_print, "print", 0);
  global->set_own(rt, rt->intern("print"), fn);

  // Symbol — exposes well-known symbols as regular string-keyed properties
  Value sym_obj = Object::create(rt, Value::undefined_(), ClassID::object);
  auto *sym     = sym_obj.as<Object>();
  auto set_sym  = [&](const char *name, Atom well_known) { sym->set_own(rt, rt->intern(name), Value::symbol_from_atom(well_known)); };
  set_sym("iterator", rt->well_known.symbol_iterator);
  set_sym("asyncIterator", rt->well_known.symbol_asyncIterator);
  set_sym("toPrimitive", rt->well_known.symbol_toPrimitive);
  set_sym("toStringTag", rt->well_known.symbol_toStringTag);
  set_sym("hasInstance", rt->well_known.symbol_hasInstance);
  global->set_own(rt, rt->intern("Symbol"), sym_obj);
}

} // namespace qjsp
