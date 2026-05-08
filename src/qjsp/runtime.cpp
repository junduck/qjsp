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
  atom_table.reserve(static_cast<size_t>(AtomEnum::end));
  atom_table.push_back(nullptr);
  atom_types_.push_back(AtomType::string);

  // Predefined atoms: index 0 is null placeholder, start from 1
  for (size_t i = 1; i < std::size(kAtomNames); ++i) {
    auto sv  = kAtomNames[i];
    auto *s  = String::allocate_raw(sv);
    auto type = atom_type_for(static_cast<AtomEnum>(i));
    if (!s)
      return false;
    s->set_interned();
    atom_table.push_back(s);
    atom_types_.push_back(type);
    // Only string atoms go into the lookup map.
    // Symbol atoms are identified by their index only.
    if (type == AtomType::string)
      atom_map.emplace(s->view(), static_cast<Atom>(i));
  }
  return true;
}

Atom Runtime::intern(std::string_view sv) {
  auto it = atom_map.find(sv);
  if (it != atom_map.end())
    return it->second;
  auto *s = String::allocate_raw(sv);
  s->set_interned();
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_types_.push_back(AtomType::string);
  atom_map.emplace(s->view(), idx);
  return idx;
}

Atom Runtime::intern_copy(String *s) {
  if (!s)
    return kAtomNull;
  auto it = atom_map.find(s->view());
  if (it != atom_map.end()) {
    s->unref(); // already interned — free the duplicate
    return it->second;
  }
  s->set_interned();
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_types_.push_back(AtomType::string);
  atom_map.emplace(s->view(), idx);
  return idx;
}

Atom Runtime::create_symbol(std::string_view desc) {
  Atom idx = static_cast<Atom>(atom_table.size());
  auto *s  = !desc.empty() ? String::allocate_raw(desc) : nullptr;
  atom_table.push_back(s);
  atom_types_.push_back(AtomType::symbol);
  return idx;
}

Value Runtime::atom_to_value(Atom a) const {
  if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
    return Value::undefined_();
  auto *s = atom_table[a];
  if (!s)
    return Value::undefined_();
  s->ref(); // Value will own its own ref; atom_table keeps its ref
  return Value::string(s);
}

bool Runtime::init_class_table() {
  classes.resize(static_cast<size_t>(ClassID::init_count));
  for (int i = static_cast<int>(ClassID::object); i < static_cast<int>(ClassID::init_count); ++i) {
    classes[static_cast<size_t>(i)].class_id   = static_cast<uint32_t>(i);
    classes[static_cast<size_t>(i)].class_name = kAtomNull;
  }
  return true;
}

Shape *Runtime::add_shape(Shape *from, Atom atom, int flags) {
  ShapeKey key{from, atom, flags};
  auto it = shape_cache.find(key);
  if (it != shape_cache.end())
    return it->second;

  auto *s  = new Shape();
  auto cnt = from ? from->prop_count : uint32_t{0};
  s->prop_count = cnt + 1;
  s->entries    = std::make_unique<ShapeProperty[]>(s->prop_count);
  if (from && cnt > 0)
    std::copy(from->entries.get(), from->entries.get() + cnt, s->entries.get());
  s->entries[cnt] = {atom, flags};
  shape_cache.emplace(key, s);
  return s;
}

// ─── GC ────────────────────────────────────────────────────────────────────

static void mark_object(GCObjectHeader *hdr, std::vector<GCObjectHeader *> &worklist) {
  switch (hdr->gc_obj_type) {
  case GCObjType::js_object:
    static_cast<Object *>(hdr)->gc_mark(worklist);
    break;
  case GCObjType::js_context:
    static_cast<Context *>(hdr)->gc_mark(worklist);
    break;
  case GCObjType::function_bytecode:
    static_cast<FunctionBytecode *>(hdr)->gc_mark(worklist);
    break;
  default:
    hdr->is_marked = true;
    break;
  }
}

static void gc_free_object(GCObjectHeader *p) {
  switch (p->gc_obj_type) {
  case GCObjType::js_object: {
    auto *obj = static_cast<Object *>(p);
    obj->var_refs.clear();
    delete obj;
    break;
  }
  case GCObjType::js_context:
    delete static_cast<Context *>(p);
    break;
  case GCObjType::function_bytecode: {
    delete static_cast<FunctionBytecode *>(p);
    break;
  }
  default:
    break;
  }
}

void Runtime::run_gc() {
  gc_phase = GCPhase::remove_cycles;

  for (auto *obj : gc_objects)
    obj->is_marked = false;

  std::vector<GCObjectHeader *> mark_worklist;
  for (auto *obj : gc_objects)
    if (obj->gc_obj_type == GCObjType::js_context)
      mark_object(obj, mark_worklist);

  while (!mark_worklist.empty()) {
    auto *p = mark_worklist.back();
    mark_worklist.pop_back();
    mark_object(p, mark_worklist);
  }

  // Sweep: swap-pop unmarked objects.
  size_t i = 0;
  while (i < gc_objects.size()) {
    auto *p = gc_objects[i];
    if (!p->is_marked) {
      gc_objects[i] = gc_objects.back();
      gc_objects.pop_back();
      gc_free_object(p);
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
