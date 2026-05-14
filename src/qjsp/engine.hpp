#pragma once

#include "atom.hpp"
#include "class.hpp"
#include "gc.hpp"
#include "object.hpp"
#include "shape.hpp"
#include "string.hpp"
#include "value.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qjsp {
namespace details {
struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
};

inline void hash_combine(size_t &seed, size_t v) { seed ^= v + 0x9e3779b9u + (seed << 6) + (seed >> 2); }

} // namespace details

constexpr inline size_t kInterruptCounterInit = 10000;
constexpr inline size_t kDefaultStackSize     = 1024 * 1024;

// ─── Engine ─────────────────────────────────────────────────────────────────
//
// Engine merges what were previously Runtime (backend: atoms, GC, class table,
// shape cache, host callbacks) and Context (execution realm: built-in values,
// global scope) into a single class.
//
// Engine itself is NOT a GC-tracked object — it owns the GC machinery.
// GC roots are marked directly from Engine::gc_mark_roots().

struct Engine {
  // ── atom table (was Runtime) ─────────────────────────────────────────────
  struct AtomTable {
    std::unique_ptr<StrPrim> atom;
    bool is_symbol;
  };
  std::vector<AtomTable> atom_table;
  std::unordered_map<std::string_view, Atom, details::StringHash, std::equal_to<>> atom_map;

  std::vector<Atom> known; // WellKnown::ENUM_NAME -> atom

  // ── shape cache (was Runtime) ────────────────────────────────────────────
  struct ShapeKey {
    Shape *from;
    Atom atom;
    int flags;
    bool operator==(const ShapeKey &o) const = default;
  };
  struct ShapeKeyHash {
    size_t operator()(const ShapeKey &k) const {
      size_t h = reinterpret_cast<uintptr_t>(k.from);
      details::hash_combine(h, k.atom);
      details::hash_combine(h, static_cast<size_t>(k.flags));
      return h;
    }
  };
  // NOTE: holding Shape*, manual life
  std::unordered_map<ShapeKey, std::unique_ptr<Shape>, ShapeKeyHash> shapes;

  // ── Built-in prototypes ──────────────────────────────────────────────

  // Flat array indexed by Builtin enum. Each entry holds the prototype
  // object for that builtin type. Populated by init_builtins().
  std::unique_ptr<Value[]> builtin_protos;

  Value get_proto(Builtin id) const {
    return builtin_protos[static_cast<size_t>(id)];
  }

  // ── GC ─────────────────────────────────────────────────────

  GCObjList gc_objects;
  GCObjList weakrefs;

  size_t gc_alloc_count      = 0;
  size_t malloc_gc_threshold = 1024;

  Value global_obj     = Value::undefined_();
  Value global_var_obj = Value::undefined_();

  // ── lifecycle ────────────────────────────────────────────────────

  Engine();

  // ── atoms ────────────────────────────────────────────────────────

  bool init_atoms();
  void init_builtins();
  Atom intern(std::string_view sv);
  Atom intern_copy(StrPrim *s);
  Value atom_to_value(Atom a) const;
  std::string_view atom_view(Atom a) const {
    if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
      return {};
    auto const &atom = atom_table[a].atom;
    return atom ? atom->view() : std::string_view{};
  }
  Atom create_symbol(std::string_view desc = {});
  bool atom_is_symbol(Atom a) const { return a < atom_table.size() && atom_table[a].is_symbol; }

  // ── GC ───────────────────────────────────────────────────────────────────

  void add_gc_object(GCObjectHeader *obj) { gc_objects.push_back(obj); }
  void remove_gc_object(GCObjectHeader *obj) {
    auto it = std::find(gc_objects.begin(), gc_objects.end(), obj);
    if (it != gc_objects.end()) {
      std::swap(*it, gc_objects.back());
      gc_objects.pop_back();
    }
  }

  void run_gc();
  void maybe_trigger_gc(size_t size_hint = 0);

  /// Mark GC roots directly from Engine's member values.
  /// Replaces the old pattern of iterating Context objects in gc_objects.
  void gc_mark_roots(std::vector<GCObjectHeader *> &worklist);

  // ── shapes ───────────────────────────────────────────────────────────────

  Shape *add_shape(Shape *from, Atom atom, int flags);

  // ── resources ───────────────────────────────────────────────────────────────

  Value create_object(Value proto, Builtin class_id = Builtin::object);
};

} // namespace qjsp
