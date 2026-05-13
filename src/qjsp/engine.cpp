#include "qjsp/engine.hpp"
#include "qjsp/array.hpp"
#include "qjsp/class.hpp"
#include "qjsp/object.hpp"
#include "qjsp/regexp.hpp"
#include "qjsp/shape.hpp"
#include "qjsp/string.hpp"
#include <algorithm>

namespace qjsp {

static StrPrim *alloc_string(std::string_view sv) {
  auto *s = StrPrim::allocate_raw(sv);
  if (s)
    s->set_interned();
  return s;
}

static Value builtin_print(Engine * /*e*/, Value /*this_val*/, int argc, const Value *argv) {
  for (int i = 0; i < argc; ++i) {
    if (i > 0)
      std::putchar(' ');
    if (argv[i].is_string()) {
      auto *s = argv[i].as<StrPrim>();
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

// ── Engine ──────────────────────────────────────────────────────────────────

Engine::Engine() {
  // ── Runtime init ────────────────────────────────────────────────────────
  init_atoms();

  // ── global object ───────────────────────────────────────────────────────
  global_obj = Object::create(this, Value::undefined_(), Builtin::object);

  // ── built-in setup (inline, will use Engine* API in final migration) ────

  // setup_global — wires print() and Symbol onto global_obj
  {
    auto *g = global_obj.as<Object>();

    // print: CFunction — inlined because CFunctionObj::create still takes Context*
    {
      auto *cfo        = new CFunctionObj();
      cfo->ref_count   = 1;
      cfo->gc_obj_type = GCObjType::js_object;
      cfo->clsid       = Builtin::object;
      cfo->fn          = builtin_print;
      add_gc_object(cfo);
      auto fn_val = Value::object(cfo);
      g->set_own(this, intern("print"), fn_val);
    }
    // Symbol object
    auto sym_val  = Object::create(this, Value::undefined_(), Builtin::object);
    auto *sym_obj = sym_val.as<Object>();
    for (auto i = WellKnown::SymbolBegin; i < WellKnown::Count; ++i) {
      sym_obj->set_own(this, known[i], Value::symbol_from_atom(known[i]));
    }
    g->set_own(this, intern("Symbol"), sym_val);
  }
#if DISABLE_CODEBLOCK
  // init_array_prototype — wires Array.prototype
  {
    auto array_proto = Object::create(this, Value::undefined_(), Builtin::array);
    auto *proto      = array_proto.as<Object>();

    // Symbol.iterator — TODO: wire after CFunction typedef migration
    {
      auto *cfo        = new CFunctionObj();
      cfo->ref_count   = 1;
      cfo->gc_obj_type = GCObjType::js_object;
      cfo->class_id    = ClassID::c_function;
      cfo->fn          = nullptr; // TODO: wire array_values
      add_gc_object(cfo);
      obj_set_own(this, proto, known.symbol_iterator, Value::object(cfo));
    }

    // push — TODO: wire after CFunction typedef migration
    {
      auto *cfo        = new CFunctionObj();
      cfo->ref_count   = 1;
      cfo->gc_obj_type = GCObjType::js_object;
      cfo->class_id    = ClassID::c_function;
      cfo->fn          = nullptr; // TODO: wire array_push
      add_gc_object(cfo);
      obj_set_own(this, proto, intern("push"), Value::object(cfo));
    }
  }

  // init_regexp_prototype — wires RegExp.prototype
  {
    regexp_proto = obj_create(this, Value::undefined_(), ClassID::regexp);
    auto *proto  = regexp_proto.as<Object>();

    // test — TODO: wire after CFunction typedef migration
    {
      auto *cfo        = new CFunctionObj();
      cfo->ref_count   = 1;
      cfo->gc_obj_type = GCObjType::js_object;
      cfo->class_id    = ClassID::c_function;
      cfo->fn          = nullptr; // TODO: wire regexp_test
      add_gc_object(cfo);
      obj_set_own(this, proto, intern("test"), Value::object(cfo));
    }
  }
#endif
}

// ── atoms ───────────────────────────────────────────────────────────────────

bool Engine::init_atoms() {
  atom_table.emplace_back(nullptr, false); // index 0 = kAtomNull

  // Atom
  for (auto i = WellKnown::AtomBegin; i < WellKnown::SymbolBegin; ++i) {
    auto *s = alloc_string(WellKnown::names[i]);
    if (!s)
      return false;
    auto atom = static_cast<Atom>(atom_table.size());
    atom_table.emplace_back(std::unique_ptr<StrPrim>{s}, false);
    atom_map.emplace(s->view(), atom);
    known.emplace_back(atom);
  }

  // Symbol
  for (auto i = WellKnown::SymbolBegin; i < WellKnown::Count; ++i) {
    auto *s = alloc_string(WellKnown::names[i]);
    if (!s)
      return false;
    auto atom = static_cast<Atom>(atom_table.size());
    atom_table.emplace_back(std::unique_ptr<StrPrim>{s}, true);
    known.emplace_back(atom);
  }

  return true;
}

Atom Engine::intern(std::string_view sv) {
  auto it = atom_map.find(sv);
  if (it != atom_map.end())
    return it->second;
  auto *s  = alloc_string(sv);
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.emplace_back(std::unique_ptr<StrPrim>{s}, false);
  atom_map.emplace(s->view(), idx);
  return idx;
}

Atom Engine::intern_copy(StrPrim *s) {
  if (!s)
    return kAtomNull;
  auto it = atom_map.find(s->view());
  if (it != atom_map.end()) {
    s->unref();
    return it->second;
  }
  s->set_interned();
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.emplace_back(std::unique_ptr<StrPrim>{s}, false);
  atom_map.emplace(s->view(), idx);
  return idx;
}

Atom Engine::create_symbol(std::string_view desc) {
  Atom idx = static_cast<Atom>(atom_table.size());
  auto *s  = !desc.empty() ? alloc_string(desc) : nullptr;
  atom_table.emplace_back(std::unique_ptr<StrPrim>{s}, true);
  return idx;
}

Value Engine::atom_to_value(Atom a) const {
  if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
    return Value::undefined_();
  if (atom_is_symbol(a))
    return Value::symbol_from_atom(a);
  auto const &s = atom_table[a].atom;
  if (!s)
    return Value::undefined_();
  s->ref();
  return Value::string(s.get());
}

// ── shapes ──────────────────────────────────────────────────────────────────

Shape *Engine::add_shape(Shape *from, Atom atom, int flags) {
  ShapeKey key{from, atom, flags};
  auto it = shapes.find(key);
  if (it != shapes.end())
    return it->second.get();

  auto shape        = std::make_unique<Shape>();
  auto cnt          = from ? from->prop_count : 0;
  shape->prop_count = cnt + 1;
  shape->entries    = std::make_unique<ShapeProperty[]>(shape->prop_count);
  if (from && cnt > 0) {
    auto *base = from->entries.get();
    std::copy(base, base + cnt, shape->entries.get());
  }
  shape->entries[cnt] = {atom, flags};
  shapes.emplace(key, std::move(shape));
}

// ── resources ───────────────────────────────────────────────────────────────

Value Engine::create_object(Value proto, Builtin class_id) { // TODO: remove this method
  maybe_trigger_gc();
  auto *obj        = new Object();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->extensible  = true;
  obj->clsid       = class_id;
  obj->proto       = proto;
  add_gc_object(obj);
  return Value::object(obj);
}

// ── GC ──────────────────────────────────────────────────────────────────────

void Engine::gc_mark_roots(std::vector<GCObjectHeader *> &worklist) {
  // Mark global_obj
  if (global_obj.is_object()) {
    auto *obj = global_obj.as<Object>();
    if (obj && !obj->is_marked) {
      obj->is_marked = true;
      worklist.push_back(obj);
    }
  }
  //? Mark builtins
  for (auto i = 0u; i < builtins_count; ++i) {
    if (builtins[i].proto.is_object()) {
      auto *obj = builtins[i].proto.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  }
}

void Engine::run_gc() {
  // GCPhase::remove_cycles;

  for (auto *obj : gc_objects)
    obj->is_marked = false;

  // Mark roots directly from Engine — no longer iterating Context objects.
  std::vector<GCObjectHeader *> mark_worklist;
  gc_mark_roots(mark_worklist);

  while (!mark_worklist.empty()) {
    auto *p = mark_worklist.back();
    mark_worklist.pop_back();
    p->gc_mark(mark_worklist);
  }

  size_t i = 0;
  while (i < gc_objects.size()) {
    auto *p = gc_objects[i];
    if (!p->is_marked) {
      gc_objects[i] = gc_objects.back();
      gc_objects.pop_back();
      delete p;
    } else {
      ++i;
    }
  }

  //  GCPhase::none;
  gc_alloc_count      = 0;
  malloc_gc_threshold = malloc_gc_threshold > 0 ? malloc_gc_threshold + (malloc_gc_threshold >> 1) : 1024;
}

void Engine::maybe_trigger_gc(size_t) {
  if (++gc_alloc_count >= malloc_gc_threshold)
    run_gc();
}

} // namespace qjsp
