#include "qjsp/runtime.hpp"
#include "qjsp/class.hpp"
#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/shape.hpp"
#include "qjsp/string.hpp"

#include <new>

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
      s->free();
  }
  for (auto &[_, s] : shape_cache)
    delete s;
  delete this;
}

bool Runtime::init_atoms() {
  atom_table.reserve(static_cast<size_t>(AtomEnum::end));
  atom_table.push_back(nullptr);

  for (int i = 0; i < static_cast<int>(std::size(kAtomNames)); ++i) {
    auto sv = kAtomNames[i];
    auto *s = String::create(sv);
    if (!s)
      return false;
    s->set_atom(static_cast<uint8_t>(atom_type_for(static_cast<AtomEnum>(i + 1))));
    atom_table.push_back(s);
    atom_map.emplace(std::string{s->data, s->len()}, static_cast<Atom>(atom_table.size() - 1));
  }
  return true;
}

Atom Runtime::intern(String *s) {
  if (!s)
    return kAtomNull;
  if (s->atom() != 0) {
    for (Atom i = 1; i < static_cast<Atom>(atom_table.size()); ++i)
      if (atom_table[i] == s)
        return i;
    return kAtomNull;
  }
  auto it = atom_map.find(std::string_view{s->data, s->len()});
  if (it != atom_map.end())
    return it->second;
  s->set_atom(static_cast<uint8_t>(AtomType::string));
  Atom idx = static_cast<Atom>(atom_table.size());
  atom_table.push_back(s);
  atom_map.emplace(std::string{s->data, s->len()}, idx);
  return idx;
}

String *Runtime::atom_to_string(Atom a) const {
  if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
    return nullptr;
  return atom_table[a];
}

bool Runtime::init_class_table() {
  classes.resize(static_cast<size_t>(ClassID::init_count));
  for (int i = static_cast<int>(ClassID::object); i < static_cast<int>(ClassID::init_count); ++i) {
    classes[static_cast<size_t>(i)].class_id = static_cast<uint32_t>(i);
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
  default:
    hdr->is_marked = true;
    break;
  }
}

static void gc_free_object(GCObjectHeader *p) {
  switch (p->gc_obj_type) {
  case GCObjType::js_object:
    delete static_cast<Object *>(p);
    break;
  case GCObjType::js_context:
    delete static_cast<Context *>(p);
    break;
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

  gc_phase = GCPhase::none;
  gc_alloc_count = 0;
  malloc_gc_threshold = malloc_gc_threshold > 0 ? malloc_gc_threshold + (malloc_gc_threshold >> 1) : 1024;
}

void Runtime::maybe_trigger_gc(size_t) {
  if (++gc_alloc_count >= malloc_gc_threshold)
    run_gc();
}

} // namespace qjsp
