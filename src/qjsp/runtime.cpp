#include "qjsp/runtime.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/class.hpp"
#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/shape.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"
#include <algorithm>

namespace qjsp {

static StrPrim *alloc_string(std::string_view sv) {
  auto *s = StrPrim::allocate_raw(sv);
  if (s)
    s->set_interned();
  return s;
}

Runtime::Runtime() {
  init_atoms();
  init_class_table();
}

Runtime::~Runtime() {
  for (auto *s : atom_table) {
    if (s)
      s->unref();
  }
  for (auto &[_, s] : shape_cache)
    delete s;
}

bool Runtime::init_atoms() {
  atom_table.push_back(nullptr); // index 0 = kAtomNull
  atom_is_symbol_.push_back(false);

  auto add = [&](std::string_view sv, Atom &slot, bool is_symbol = false) {
    auto *s = alloc_string(sv);
    if (!s)
      return false;
    slot = static_cast<Atom>(atom_table.size());
    atom_table.push_back(s);
    atom_is_symbol_.push_back(is_symbol);
    if (!is_symbol)
      atom_map.emplace(s->view(), slot);
    return true;
  };

  if (!add("", well_known.empty_string))
    return false;
  if (!add("prototype", well_known.prototype))
    return false;
  if (!add("constructor", well_known.constructor))
    return false;
  if (!add("length", well_known.length))
    return false;
  if (!add("name", well_known.name))
    return false;
  if (!add("toString", well_known.toString))
    return false;
  if (!add("valueOf", well_known.valueOf))
    return false;
  if (!add("eval", well_known.eval))
    return false;
  if (!add("undefined", well_known.undefined))
    return false;
  if (!add("of", well_known.of))
    return false;
  if (!add("__proto__", well_known.__proto__))
    return false;

  // Well-known symbols
  if (!add("Symbol.iterator", well_known.symbol_iterator, true))
    return false;
  if (!add("Symbol.asyncIterator", well_known.symbol_asyncIterator, true))
    return false;
  if (!add("Symbol.toPrimitive", well_known.symbol_toPrimitive, true))
    return false;
  if (!add("Symbol.toStringTag", well_known.symbol_toStringTag, true))
    return false;
  if (!add("Symbol.hasInstance", well_known.symbol_hasInstance, true))
    return false;
  if (!add("Symbol.species", well_known.symbol_species, true))
    return false;

  return true;
}

Atom Runtime::intern(std::string_view sv) {
  auto it = atom_map.find(sv);
  if (it != atom_map.end())
    return it->second;
  auto *s  = alloc_string(sv);
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_is_symbol_.push_back(false);
  atom_map.emplace(s->view(), idx);
  return idx;
}

Atom Runtime::intern_copy(StrPrim *s) {
  if (!s)
    return kAtomNull;
  auto it = atom_map.find(s->view());
  if (it != atom_map.end()) {
    s->unref();
    return it->second;
  }
  s->set_interned();
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_is_symbol_.push_back(false);
  atom_map.emplace(s->view(), idx);
  return idx;
}

Atom Runtime::create_symbol(std::string_view desc) {
  Atom idx = static_cast<Atom>(atom_table.size());
  auto *s  = !desc.empty() ? alloc_string(desc) : nullptr;
  atom_table.push_back(s);
  atom_is_symbol_.push_back(true);
  return idx;
}

Value Runtime::atom_to_value(Atom a) const {
  if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
    return Value::undefined_();
  if (atom_is_symbol(a))
    return Value::symbol_from_atom(a);
  auto *s = atom_table[a];
  if (!s)
    return Value::undefined_();
  s->ref();
  return Value::string(s);
}

bool Runtime::init_class_table() {
  class_count = std::max<uint32_t>(static_cast<uint16_t>(ClassID::init_count), 1u);
  classes     = std::make_unique<Class[]>(class_count);
  for (uint32_t i = static_cast<uint32_t>(ClassID::object); i < class_count; ++i) {
    classes[i].class_id   = static_cast<ClassID>(i);
    classes[i].class_name = kAtomNull;
  }
  return true;
}

Shape *Runtime::add_shape(Shape *from, Atom atom, int flags) {
  ShapeKey key{from, atom, flags};
  auto it = shape_cache.find(key);
  if (it != shape_cache.end())
    return it->second;

  auto *s       = new Shape();
  auto cnt      = from ? from->prop_count : uint32_t{0};
  s->prop_count = cnt + 1;
  s->entries    = std::make_unique<ShapeProperty[]>(s->prop_count);
  if (from && cnt > 0)
    std::copy(from->entries.get(), from->entries.get() + cnt, s->entries.get());
  s->entries[cnt] = {atom, flags};
  shape_cache.emplace(key, s);
  return s;
}

// ─── GC ────────────────────────────────────────────────────────────────────

void Runtime::run_gc() {
  gc_phase = GCPhase::remove_cycles;

  for (auto *obj : gc_objects)
    obj->is_marked = false;

  std::vector<GCObjectHeader *> mark_worklist;
  for (auto *obj : gc_objects)
    if (obj->gc_obj_type == GCObjType::js_context)
      obj->gc_mark(mark_worklist);

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

  gc_phase            = GCPhase::none;
  gc_alloc_count      = 0;
  malloc_gc_threshold = malloc_gc_threshold > 0 ? malloc_gc_threshold + (malloc_gc_threshold >> 1) : 1024;
}

void Runtime::maybe_trigger_gc(size_t) {
  if (++gc_alloc_count >= malloc_gc_threshold)
    run_gc();
}

} // namespace qjsp
