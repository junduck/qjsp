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
  init_atoms();

  builtin_protos = std::make_unique<Value[]>(static_cast<size_t>(Builtin::BuiltinCount));

  global_obj = Object::create(this, Value::undefined_(), Builtin::object);

  auto *g = global_obj.as<Object>();

  {
    auto fn_val = CFunctionObj::create(this, builtin_print, "print", 1);
    g->set_own(this, intern("print"), fn_val);
  }

  {
    auto sym_val  = Object::create(this, Value::undefined_(), Builtin::object);
    auto *sym_obj = sym_val.as<Object>();
    for (auto i = WellKnown::SymbolBegin; i < WellKnown::Count; ++i)
      sym_obj->set_own(this, known[i], Value::symbol_from_atom(known[i]));
    g->set_own(this, intern("Symbol"), sym_val);
  }

  init_builtins();
}

void Engine::init_builtins() {
  Object::setup(this);
  ArrayObject::setup(this);
  RegExpObj::setup(this);
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
  shape->entries[cnt]  = {atom, flags};
  auto [it2, inserted] = shapes.emplace(key, std::move(shape));
  return it2->second.get();
}

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
  if (global_obj.is_object()) {
    auto *obj = global_obj.as<Object>();
    if (obj && !obj->is_marked) {
      obj->is_marked = true;
      worklist.push_back(obj);
    }
  }
  for (size_t i = 0; i < static_cast<size_t>(Builtin::BuiltinCount); ++i) {
    auto &v = builtin_protos[i];
    if (v.is_object()) {
      auto *obj = v.as<Object>();
      if (obj && !obj->is_marked) {
        obj->is_marked = true;
        worklist.push_back(obj);
      }
    }
  }
}

void Engine::run_gc() {
  // Phase 1: Clear marks
  for (auto *obj : gc_objects)
    obj->is_marked = false;

  // Phase 2: Mark from roots
  std::vector<GCObjectHeader *> worklist;
  gc_mark_roots(worklist);
  while (!worklist.empty()) {
    auto *p = worklist.back();
    worklist.pop_back();
    p->gc_mark(worklist);
  }

  // Phase 3: Partition into survivors (marked) and candidates (unmarked)
  std::vector<GCObjectHeader *> candidates;
  size_t write = 0;
  for (size_t read = 0; read < gc_objects.size(); ++read) {
    if (gc_objects[read]->is_marked) {
      gc_objects[write++] = gc_objects[read];
    } else {
      candidates.push_back(gc_objects[read]);
    }
  }
  gc_objects.resize(write);

  if (candidates.empty()) {
    gc_alloc_count      = 0;
    malloc_gc_threshold = malloc_gc_threshold > 0 ? malloc_gc_threshold + (malloc_gc_threshold >> 1) : 1024;
    return;
  }

  // Phase 4: Trial deletion — gc_refs = ref_count, then subtract internal refs
  for (auto *c : candidates)
    c->gc_refs = c->ref_count;
  for (auto *c : candidates)
    c->gc_decref_refs();

  // Phase 5: Revive candidates with external references (gc_refs > 0)
  for (auto *c : candidates) {
    if (c->gc_refs > 0 && !c->is_marked) {
      c->is_marked = true;
      worklist.push_back(c);
    }
  }
  while (!worklist.empty()) {
    auto *p = worklist.back();
    worklist.pop_back();
    p->gc_mark(worklist);
  }

  // Phase 6: Break refs in dead candidates, then delete
  for (auto *c : candidates) {
    if (!c->is_marked)
      c->gc_clear_refs();
  }
  for (auto *c : candidates) {
    if (c->is_marked) {
      gc_objects.push_back(c);
    } else {
      delete c;
    }
  }

  gc_alloc_count      = 0;
  malloc_gc_threshold = malloc_gc_threshold > 0 ? malloc_gc_threshold + (malloc_gc_threshold >> 1) : 1024;
}

void Engine::maybe_trigger_gc(size_t) {
  if (++gc_alloc_count >= malloc_gc_threshold)
    run_gc();
}

} // namespace qjsp
