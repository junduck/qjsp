#pragma once

#include "atom.hpp"
#include "gc.hpp"
#include "string.hpp"
#include "value.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qjsp {

struct Context;
struct Shape;
struct Class;
struct ModuleDef;
struct JobEntry;
struct StrPrim;
struct Object;

struct MallocState {
  size_t malloc_count;
  size_t malloc_size;
  size_t malloc_limit;
  void *opaque;
};
struct MallocFunctions {
  void *(*js_malloc)(MallocState *s, size_t size);
  void (*js_free)(MallocState *s, void *ptr);
  void *(*js_realloc)(MallocState *s, void *ptr, size_t size);
  size_t (*js_malloc_usable_size)(const void *ptr);
};
using ModuleNormalizeFunc                  = char *(void *ctx, const char *base_name, const char *name, void *opaque);
using ModuleLoaderFunc                     = ModuleDef *(void *ctx, const char *module_name, void *opaque);
using ModuleLoaderFunc2                    = ModuleDef *(void *ctx, const char *module_name, void *opaque, Value attributes);
using ModuleCheckSupportedImportAttributes = int(void *ctx, void *opaque, Value attributes);
using InterruptHandler                     = int(void *rt, void *opaque);
using HostPromiseRejectionTracker          = void(void *ctx, Value promise, Value reason, int is_handled, void *opaque);
using JobFunc                              = Value(void *ctx, int argc, const Value *argv);
using SABAllocFunc                         = uint8_t *(void *opaque, size_t size);
using SABFreeFunc                          = void(void *opaque, uint8_t *ptr);
using SABDupFunc                           = uint8_t *(void *opaque, uint8_t *ptr);

struct SharedArrayBufferFunctions {
  SABAllocFunc *sab_alloc;
  SABFreeFunc *sab_free;
  SABDupFunc *sab_dup;
  void *sab_opaque;
};

struct StringHash {
  using is_transparent = void;
  size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
};

inline void hash_combine(size_t &seed, size_t v) { seed ^= v + 0x9e3779b9u + (seed << 6) + (seed >> 2); }

struct Runtime {
  MallocFunctions mf{};
  MallocState malloc_state{};
  const char *rt_info = nullptr;

  std::vector<StrPrim *> atom_table;
  std::vector<bool> atom_is_symbol_;
  std::unordered_map<std::string_view, Atom, StringHash, std::equal_to<>> atom_map;

  /// Eagerly-interned atoms the engine references by index.
  /// Populated during init_atoms() and stable thereafter.
  struct WellKnownAtoms {
    Atom empty_string         = 0;
    Atom prototype            = 0;
    Atom constructor          = 0;
    Atom length               = 0;
    Atom name                 = 0;
    Atom toString             = 0;
    Atom valueOf              = 0;
    Atom eval                 = 0;
    Atom undefined            = 0;
    Atom of                   = 0;
    Atom __proto__            = 0;
    Atom symbol_iterator      = 0;
    Atom symbol_asyncIterator = 0;
    Atom symbol_toPrimitive   = 0;
    Atom symbol_toStringTag   = 0;
    Atom symbol_hasInstance   = 0;
    Atom symbol_species       = 0;
  } well_known;

  std::unique_ptr<Class[]> classes;
  uint32_t class_count = 0;

  GCObjList gc_objects;
  GCObjList weakrefs;

  GCPhase gc_phase           = GCPhase::none;
  size_t gc_alloc_count      = 0;
  size_t malloc_gc_threshold = 1024;

  uintptr_t stack_size  = kDefaultStackSize;
  uintptr_t stack_top   = 0;
  uintptr_t stack_limit = 0;

  Value current_exception               = Value::uninitialized();
  bool current_exception_is_uncatchable = false;
  bool in_out_of_memory                 = false;

  InterruptHandler *interrupt_handler                         = nullptr;
  void *interrupt_opaque                                      = nullptr;
  HostPromiseRejectionTracker *host_promise_rejection_tracker = nullptr;
  void *host_promise_rejection_tracker_opaque                 = nullptr;

  ModuleNormalizeFunc *module_normalize_func = nullptr;
  bool module_loader_has_attr                = false;
  union {
    ModuleLoaderFunc *module_loader_func = nullptr;
    ModuleLoaderFunc2 *module_loader_func2;
  } u;
  ModuleCheckSupportedImportAttributes *module_check_attrs = nullptr;
  void *module_loader_opaque                               = nullptr;
  int64_t module_async_evaluation_next_timestamp           = 0;

  bool can_block = false;
  SharedArrayBufferFunctions sab_funcs{};
  uint8_t strip_flags = 0;

  struct ShapeKey {
    Shape *from;
    Atom atom;
    int flags;
    bool operator==(const ShapeKey &o) const = default;
  };
  struct ShapeKeyHash {
    size_t operator()(const ShapeKey &k) const {
      size_t h = reinterpret_cast<uintptr_t>(k.from);
      hash_combine(h, k.atom);
      hash_combine(h, static_cast<size_t>(k.flags));
      return h;
    }
  };
  std::unordered_map<ShapeKey, Shape *, ShapeKeyHash> shape_cache;

  Shape *add_shape(Shape *from, Atom atom, int flags);
  void *user_opaque = nullptr;

  static constexpr size_t kDefaultStackSize = 1024 * 1024;

  Runtime();
  ~Runtime();

  bool init_atoms();
  bool init_class_table();

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

  Atom intern(std::string_view sv);
  Atom intern_copy(StrPrim *s);
  Value atom_to_value(Atom a) const;
  std::string_view atom_view(Atom a) const {
    if (a == kAtomNull || a >= static_cast<Atom>(atom_table.size()))
      return {};
    auto *s = atom_table[a];
    return s ? s->view() : std::string_view{};
  }
  Atom create_symbol(std::string_view desc = {});
  bool atom_is_symbol(Atom a) const { return a < static_cast<Atom>(atom_is_symbol_.size()) && atom_is_symbol_[a]; }
};

} // namespace qjsp
