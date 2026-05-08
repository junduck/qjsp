#include "qjsp/object.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/context.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"
#include <cstdio>

namespace qjsp {

Value Object::create(Runtime *rt, Value proto, uint16_t class_id) {
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

Value Object::make_cfunc(Context *ctx, CFunction *fn, std::string_view name, int length) {
  auto obj          = create(ctx->rt, Value::undefined_(), static_cast<int>(ClassID::c_function));
  auto *o           = obj.as<Object>();
  o->u.cfunc.fn     = fn;
  o->u.cfunc.length = static_cast<uint8_t>(length);
  o->set_own(ctx->rt, ctx->rt->intern("length"), Value::int32(length));
  o->set_own(ctx->rt, ctx->rt->intern("name"), String::create(name));
  return obj;
}

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
  var_refs.clear();
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
  if (class_id == static_cast<uint16_t>(ClassID::bytecode_function) && u.opaque) {
    auto *bc = static_cast<FunctionBytecode *>(u.opaque);
    if (!bc->is_marked) {
      bc->is_marked = true;
      worklist.push_back(bc);
    }
  }
}

// ─── call ──────────────────────────────────────────────────────────────────

Value call(Context *ctx, Value func, Value this_val, int argc, const Value *argv) {
  if (!func.is_object())
    return Value::undefined_();
  auto *obj = func.as<Object>();

  switch (static_cast<ClassID>(obj->class_id)) {
  case ClassID::c_function:
  case ClassID::c_function_data:
    if (obj->u.cfunc.fn)
      return obj->u.cfunc.fn(ctx, this_val, argc, argv);
    return Value::undefined_();

  default:
    return Value::undefined_();
  }
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
  auto fn = Object::make_cfunc(ctx, builtin_print, "print", 0);
  global->set_own(ctx->rt, ctx->rt->intern("print"), fn);
}

} // namespace qjsp
