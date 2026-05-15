// ============================================================================
#include "qjsp/array.hpp"
//  Function types + Function builtin setup
// ============================================================================

#include "qjsp/bytecode.hpp"
#include "qjsp/class.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/function.hpp"
#include "qjsp/object.hpp"
#include "qjsp/reg_interpreter.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"

namespace qjsp {

// ─── CFunctionObj ──────────────────────────────────────────────────────────

Value CFunctionObj::create(Engine *e, CFunction *fn, std::string_view name, int length) {
  auto *obj      = new CFunctionObj();
  obj->ref_count = 1;
  obj->clsid     = Builtin::object;
  obj->fn        = fn;
  obj->proto     = e->get_proto(Builtin::function);
  e->add_gc_object(obj);
  obj->set_own(e, e->intern("length"), Value::int32(length));
  obj->set_own(e, e->intern("name"), StrPrim::create(name));
  return Value::callable(obj);
}

// ─── BFunctionObj ──────────────────────────────────────────────────────

Value BFunctionObj::create(Engine *e, Bytecode *bc) {
  auto *obj        = new BFunctionObj();
  obj->ref_count   = 1;
  obj->clsid       = Builtin::object;
  obj->bytecode    = bc;
  obj->proto       = e->get_proto(Builtin::function);
  obj->is_bytecode = true;
  e->add_gc_object(obj);
  return Value::callable(obj);
}

Value BFunctionObj::call(Engine *e, Value this_val, int argc, const Value *argv) {
  std::vector<VarRef *> upvals;
  upvals.reserve(var_refs.size());
  for (auto &v : var_refs)
    upvals.push_back(v.as<VarRef>());
  RegInterpreter interp{e};
  return interp.call_bytecode(bytecode, this_val, argc, argv, upvals.data());
}

void BFunctionObj::gc_mark(std::vector<GCObjectHeader *> &worklist) {
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

void BFunctionObj::gc_decref_refs() {
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

void BFunctionObj::gc_clear_refs() {
  var_refs.clear();
  Object::gc_clear_refs();
}

namespace {
// ─── Function prototype methods ──────────────────────────────────────────────

Value function_call(Engine *e, Value this_val, int argc, const Value *argv) {
  if (!this_val.is_callable())
    return Value::undefined_();
  Value this_arg         = argc > 0 ? argv[0] : Value::undefined_();
  int call_argc          = argc > 0 ? argc - 1 : 0;
  const Value *call_argv = argc > 1 ? argv + 1 : nullptr;
  return static_cast<Callable *>(this_val.as<Object>())->call(e, this_arg, call_argc, call_argv);
}

Value function_apply(Engine *e, Value this_val, int argc, const Value *argv) {
  if (!this_val.is_callable())
    return Value::undefined_();
  Value this_arg = argc > 0 ? argv[0] : Value::undefined_();

  if (argc < 2 || !argv[1].is_object()) {
    return static_cast<Callable *>(this_val.as<Object>())->call(e, this_arg, 0, nullptr);
  }
  auto *obj = argv[1].as<Object>();
  if (obj->clsid != Builtin::array) {
    return static_cast<Callable *>(this_val.as<Object>())->call(e, this_arg, 0, nullptr);
  }
  auto *arr = static_cast<ArrayObject *>(obj);

  return static_cast<Callable *>(this_val.as<Object>())
      ->call(e, this_arg, static_cast<int>(arr->elements.size()), arr->elements.empty() ? nullptr : arr->elements.data());
}

// ─── Function constructor (stub) ─────────────────────────────────────────────

Value function_constructor(Engine *e, Value this_val, int argc, const Value *argv) {
  (void)argc;
  (void)argv;
  if (this_val.is_object())
    return this_val;
  return Object::create(e, e->get_proto(Builtin::function), Builtin::object);
}
} // namespace

// ─── call() — virtual dispatch ───────────────────────────────────────────────

Value call(Engine *e, Value func, Value this_val, int argc, const Value *argv) {
  if (!func.is_callable())
    return Value::undefined_();
  return static_cast<Callable *>(func.as<Object>())->call(e, this_val, argc, argv);
}

// ============================================================================
//  Function::setup()  —  5‑stage builtin class initialisation
// ============================================================================

void Function::setup(Engine *e) {
  constexpr auto id = Builtin::function;
  auto idx          = static_cast<size_t>(id);

  // ── Stage 1: Prototype ─────────────────────────────────────────────────
  Value proto            = Object::create(e, e->get_proto(Builtin::object), Builtin::object);
  e->builtin_protos[idx] = proto;
  auto *p                = proto.as<Object>();

  // ── Stage 2: Prototype methods ─────────────────────────────────────────
  Value call_fn = CFunctionObj::create(e, function_call, "call", 1);
  p->set_own(e, e->intern("call"), call_fn);

  Value apply_fn = CFunctionObj::create(e, function_apply, "apply", 2);
  p->set_own(e, e->intern("apply"), apply_fn);

  // ── Stage 3: Constructor ───────────────────────────────────────────────
  Value ctor = CFunctionObj::create(e, function_constructor, "Function", 1);

  // ── Stage 4: Linkage ───────────────────────────────────────────────────
  auto proto_atom = e->intern("prototype");
  auto cons_atom  = e->intern("constructor");
  ctor.as<Object>()->set_own(e, proto_atom, proto);
  proto.as<Object>()->set_own(e, cons_atom, ctor);

  // ── Stage 5: Export ────────────────────────────────────────────────────
  e->global_obj.as<Object>()->set_own(e, e->intern("Function"), ctor);

  // ── Retroactive fix ────────────────────────────────────────────────────
  //  Object constructor was created in Object::setup before Function::setup
  //  ran, so its __proto__ was undefined.  Set it to Function.prototype now.
  Value obj_ctor = e->global_obj.as<Object>()->get_own(e, e->intern("Object"));
  if (obj_ctor.is_object())
    obj_ctor.as<Object>()->proto = proto;
}

} // namespace qjsp
