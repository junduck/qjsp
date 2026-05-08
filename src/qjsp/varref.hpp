#pragma once

#include "qjsp/gc.hpp"
#include "qjsp/value.hpp"

namespace qjsp {

// VarRef — heap-boxed indirection for captured variables.
//
// While the owning stack frame is alive, pvalue points directly into
// that frame's register array (attached mode).  When the frame returns,
// the value is copied into VarRef::value and pvalue is repointed
// (detached mode).  Multiple closures sharing the same capture hold
// the same VarRef pointer — all writes are immediately visible to all
// observers regardless of attachment state.

struct VarRef : RefCounted {
  // ── creation ──────────────────────────────────────────────────────────

  // Create attached VarRef whose pvalue points at *slot.
  static Value create(Value &slot);

  // Create detached VarRef initialised to v.
  static Value create_detached(Value v);

  // ── access ────────────────────────────────────────────────────────────

  Value load() const { return *pvalue; }
  void store(Value v) { *pvalue = v; }

  // ── lifecycle ─────────────────────────────────────────────────────────

  // Detach: copy *pvalue into internal value_, repoint pvalue.
  void close() {
    if (!is_detached()) {
      value_ = *pvalue;
      pvalue = &value_;
    }
  }

  bool is_detached() const { return pvalue == &value_; }

  // ── fields ────────────────────────────────────────────────────────────

  Value *pvalue = nullptr; // → slot in frame (attached) or → value_ (detached)
private:
  Value value_ = Value::undefined_(); // internal storage when detached
};

} // namespace qjsp
