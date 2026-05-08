#include "qjsp/runtime.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/class.hpp"
#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/shape.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"

namespace qjsp {

Runtime *Runtime::create() {
  auto *rt = new Runtime();
  if (!rt->init_atoms())
    goto fail;
  if (!rt->init_class_table())
    goto fail;
  return rt;
fail:
  rt->destroy();
  return nullptr;
}

void Runtime::destroy() {
  for (auto *s : atom_table) {
    if (s)
      s->unref();
  }
  for (auto &[_, s] : shape_cache)
    delete s;
  delete this;
}

bool Runtime::init_atoms() {
  atom_table.reserve(static_cast<size_t>(AtomEnum::end));
  atom_table.push_back(nullptr);

  // Predefined atoms: index 0 is null placeholder, start from 1
  for (size_t i = 1; i < std::size(kAtomNames); ++i) {
    auto sv = kAtomNames[i];
    auto *s = String::create(sv);
    if (!s)
      return false;
    s->set_interned();
    atom_table.push_back(s);
    atom_map.emplace(s->view(), static_cast<Atom>(i));
  }
  return true;
}

Atom Runtime::intern(std::string_view sv) {
  auto it = atom_map.find(sv);
  if (it != atom_map.end())
    return it->second;
  // add new interned string
  auto s = String::create(sv);
  s->set_interned();
  auto idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_map.emplace(s->view(), idx); // should use the new mem
  return idx;
}

Atom Runtime::intern(String *s) {
  if (!s)
    return kAtomNull;
  if (s->is_interned()) {
    for (Atom i = 1; i < static_cast<Atom>(atom_table.size()); ++i)
      if (atom_table[i] == s)
        return i;
    return kAtomNull;
  }
  auto it = atom_map.find(s->view());
  if (it != atom_map.end())
    return it->second;
  s->set_interned();
  s->ref(); // copying to table
  Atom idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_map.emplace(s->view(), idx);
  return idx;
}

String *Runtime::atom_to_string(Atom a) const {
  if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
    return nullptr;
  auto s = atom_table[a];
  s->ref();
  return s;
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

  auto *s = new Shape();
  if (from)
    s->entries = from->entries;
  s->entries.push_back({atom, flags});
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
    if (obj->class_id == static_cast<uint16_t>(ClassID::bytecode_function)) {
      if (obj->var_refs) {
        for (int i = 0; i < obj->var_ref_count; i++) {
          if (obj->var_refs[i])
            obj->var_refs[i]->unref();
        }
        delete[] obj->var_refs;
        obj->var_refs = nullptr;
      }
    }
    delete obj;
    break;
  }
  case GCObjType::js_context:
    delete static_cast<Context *>(p);
    break;
  case GCObjType::function_bytecode: {
    auto *b = static_cast<FunctionBytecode *>(p);
    delete[] b->byte_code_buf;
    delete[] b->cpool;
    delete[] b->vardefs;
    delete[] b->closure_var;
    delete b;
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
