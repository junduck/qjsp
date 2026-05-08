//! NaN-boxed JavaScript value (64-bit).
//!
//! All tagged values use the quiet NaN space (top 12 bits = 0x7FF).
//! The next 4 bits (bits 48-51) encode the type tag.
//! Doubles that happen to fall in this space are canonicalized to
//! 0x7FF8000000000000 (the standard quiet NaN).
//!
//! Tag layout (prefix in bits 48-63):
//!
//!   ─────── pointer boundary ─────── RC
//!   0x7FF0  = String        (48-bit pointer)
//!   0x7FF1  = VarRef        (48-bit pointer)
//!   0x7FF2  = (reserved, intended for StringRope)
//!   ─────── pointer boundary ─────── GC
//!   0x7FF3  = Symbol        (48-bit pointer)
//!   0x7FF4  = BigInt        (48-bit pointer)
//!   0x7FF5  = Object        (48-bit pointer)
//!   0x7FF6  = Module        (48-bit pointer, internal)
//!   0x7FF7  = FuncBytecode  (48-bit pointer, internal)
//!   0x7FF8  = canonical QNaN (for legit float NaN)
//!   ─────── scala boundary ───────
//!   0x7FF9  = Exception
//!   0x7FFA  = CatchOffset   (payload = offset)
//!   0x7FFB  = ShortBigInt   (inline, lower 32 bits)
//!   0x7FFC  = Int32         (lower 32 bits)
//!   0x7FFD  = Bool          (bit 0)
//!   0x7FFE  = Null/Undef    (bit 0: 0=null, 1=undefined)
//!   0x7FFF  = Uninitialized
//!
//!   anything else = Float64
//!
//! Pointer checks:      kPtrStart <= tag_prefix() <= kPtrEnd  (0x7FF0–0x7FF7)
//! Double checks:       tag_prefix() < 0x7FF0 || tag_prefix() > 0x7FFF || tag == QNaN
//! RC-only (no vtable): tag_prefix() < 0x7FF3  (kTagSymbol)
//! GC-object (vtable):  tag_prefix() >= 0x7FF3

#pragma once

#include "qjsp/gc.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

namespace qjsp {

constexpr int kTagShift         = 48;
constexpr uint64_t kTagMask     = 0xFFFFull << kTagShift;
constexpr uint64_t kPayloadMask = (1ull << kTagShift) - 1;

// ─── Pointer tags ─── RC (0x7FF0–0x7FF2): RefCounted only, no vtable ──────
constexpr uint64_t kTagString     = 0x7FF0ull;
constexpr uint64_t kTagVarRef     = 0x7FF1ull;
constexpr uint64_t kTagStringRope = 0x7FF2ull; // reserved, not yet used

// ─── Pointer tags ─── GC (0x7FF3–0x7FF7): GCObjectHeader subclasses ───────
constexpr uint64_t kTagSymbol       = 0x7FF3ull;
constexpr uint64_t kTagBigInt       = 0x7FF4ull;
constexpr uint64_t kTagObject       = 0x7FF5ull;
constexpr uint64_t kTagModule       = 0x7FF6ull;
constexpr uint64_t kTagFuncBytecode = 0x7FF7ull;

// ─── Canonical quiet NaN ───────────────────────────────────────────────────
constexpr uint64_t kTagNaN       = 0x7FF8ull;
constexpr uint64_t kCanonicalNaN = kTagNaN << kTagShift;

// ─── Scalar (non-pointer) tags (0x7FF9–0x7FFF) ─────────────────────────────
constexpr uint64_t kTagException     = 0x7FF9ull;
constexpr uint64_t kTagCatchOffset   = 0x7FFAull;
constexpr uint64_t kTagShortBigInt   = 0x7FFBull;
constexpr uint64_t kTagInt32         = 0x7FFCull;
constexpr uint64_t kTagBool          = 0x7FFDull;
constexpr uint64_t kTagNullUndef     = 0x7FFEull;
constexpr uint64_t kTagUninitialized = 0x7FFFull;

// ─── Derived boundaries ────────────────────────────────────────────────────
constexpr uint64_t kPtrStart  = kTagString;        // first pointer tag (0x7FF0)
constexpr uint64_t kPtrEnd    = kTagFuncBytecode;  // last pointer tag (0x7FF7)
constexpr uint64_t kTaggedMin = kTagString;        // smallest tag overall (0x7FF0)
constexpr uint64_t kTaggedMax = kTagUninitialized; // largest tag overall (0x7FFF)

// ─── Value ─────────────────────────────────────────────────────────────────

struct Value {
  uint64_t data;

  constexpr Value() : data(kTagNullUndef << kTagShift) {}
  explicit constexpr Value(uint64_t d) : data(d) {}

  // ── RAII ─────────────────────────────────────────────────────────────

  Value(Value const &other) noexcept : data(other.data) {
    if (is_pointer() && !is_null_ptr())
      get_ref_counted()->ref();
  }

  Value(Value &&other) noexcept : data(other.data) { other.data = (kTagNullUndef << kTagShift) | 1ull; }

  Value &operator=(Value other) noexcept {
    swap(other);
    return *this;
  }

  void swap(Value &other) noexcept {
    using std::swap;
    swap(data, other.data);
  }

  ~Value() {
    if (is_pointer() && !is_null_ptr()) {
      auto *rc = get_ref_counted();
      rc->unref();
      if (rc->ref_count == 0) {
        if (is_string() || is_var_ref()) {
          ::operator delete(as_pointer());
        }
      }
    }
  }

  Value ref() noexcept {
    assert(is_pointer());
    if (!is_null_ptr())
      get_ref_counted()->ref();
    return *this;
  }

  // ── factories ────────────────────────────────────────────────────────

  static constexpr Value int32(int32_t v) { return Value{(kTagInt32 << kTagShift) | (static_cast<uint64_t>(static_cast<uint32_t>(v)))}; }

  static constexpr Value bool_(bool v) { return Value{(kTagBool << kTagShift) | (v ? 1ull : 0ull)}; }

  static constexpr Value null_() { return Value{kTagNullUndef << kTagShift}; }
  static constexpr Value undefined_() { return Value{(kTagNullUndef << kTagShift) | 1ull}; }
  static constexpr Value uninitialized() { return Value{kTagUninitialized << kTagShift}; }
  static constexpr Value exception() { return Value{kTagException << kTagShift}; }

  static constexpr Value catch_offset(int32_t off) {
    return Value{(kTagCatchOffset << kTagShift) | (static_cast<uint64_t>(static_cast<uint32_t>(off)))};
  }

  static constexpr Value short_big_int(int32_t v) {
    return Value{(kTagShortBigInt << kTagShift) | (static_cast<uint64_t>(static_cast<uint32_t>(v)))};
  }

  static Value object(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagObject << kTagShift) | p};
  }

  static Value string(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagString << kTagShift) | p};
  }

  static Value string_rope(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagStringRope << kTagShift) | p};
  }

  static Value symbol(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagSymbol << kTagShift) | p};
  }

  static Value bigint_ptr(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagBigInt << kTagShift) | p};
  }

  static Value module_ptr(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagModule << kTagShift) | p};
  }

  static Value func_bytecode(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagFuncBytecode << kTagShift) | p};
  }

  static Value var_ref(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    return Value{(kTagVarRef << kTagShift) | p};
  }

  static Value float64(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    // Canonicalize doubles that fall in our tagged NaN space.
    // Exception: +Inf (0x7FF0000000000000) has tag=kTagString with
    // payload=0. Since we never create Value::string(nullptr), this
    // bit pattern unambiguously means +Inf — keep it.
    if (bits == 0x7FF0000000000000)
      return Value{bits};
    uint16_t t = static_cast<uint16_t>(bits >> kTagShift);
    if (t >= kTaggedMin && t <= kTaggedMax)
      bits = kCanonicalNaN;
    return Value{bits};
  }

  // ── type queries ─────────────────────────────────────────────────────

  uint16_t tag_prefix() const { return static_cast<uint16_t>(data >> kTagShift); }

  bool is_uninitialized() const { return tag_prefix() == kTagUninitialized; }
  bool is_exception() const { return tag_prefix() == kTagException; }
  bool is_catch_offset() const { return tag_prefix() == kTagCatchOffset; }
  bool is_short_big_int() const { return tag_prefix() == kTagShortBigInt; }
  bool is_int32() const { return tag_prefix() == kTagInt32; }
  bool is_bool() const { return tag_prefix() == kTagBool; }
  bool is_null() const { return tag_prefix() == kTagNullUndef && (data & 1) == 0; }
  bool is_undefined() const { return tag_prefix() == kTagNullUndef && (data & 1) == 1; }
  bool is_null_or_undef() const { return tag_prefix() == kTagNullUndef; }

  bool is_object() const { return tag_prefix() == kTagObject; }
  bool is_string() const { return tag_prefix() == kTagString && !is_null_ptr(); }
  bool is_symbol() const { return tag_prefix() == kTagSymbol; }
  bool is_bigint_ptr() const { return tag_prefix() == kTagBigInt; }
  bool is_module() const { return tag_prefix() == kTagModule; }
  bool is_func_bytecode() const { return tag_prefix() == kTagFuncBytecode; }
  bool is_var_ref() const { return tag_prefix() == kTagVarRef; }

  bool is_pointer() const {
    uint16_t t = tag_prefix();
    return t >= kPtrStart && t <= kPtrEnd;
  }

  bool is_null_ptr() const { return (data & kPayloadMask) == 0; }

  bool is_double() const {
    // +Inf (0x7FF0000000000000) collides with kTagString+null payload;
    // we never create null-string Values, so this is unambiguously +Inf.
    if (data == 0x7FF0000000000000)
      return true;
    uint16_t t = tag_prefix();
    return t == kTagNaN || t < kTaggedMin || t > kTaggedMax;
  }

  bool is_number() const { return is_double() || is_int32(); }
  bool is_bigint() const { return is_short_big_int() || is_bigint_ptr(); }

  bool has_ref_count() const { return is_pointer(); }

  // ── extractors ───────────────────────────────────────────────────────

  int32_t as_int32() const { return static_cast<int32_t>(data & 0xFFFFFFFFu); }
  bool as_bool() const { return (data & 1) != 0; }
  int32_t as_catch_offset() const { return static_cast<int32_t>(data & 0xFFFFFFFFu); }
  int32_t as_short_big_int() const { return static_cast<int32_t>(data & 0xFFFFFFFFu); }

  void *as_pointer() const { return reinterpret_cast<void *>(data & kPayloadMask); }

  RefCounted *get_ref_counted() const {
    uintptr_t raw = static_cast<uintptr_t>(data & kPayloadMask);
    if (raw == 0)
      return nullptr;
    // GC-objects (0x7FF3–0x7FF7): GCObjectHeader subclasses with vtable.
    // Use static_cast through GCObjectHeader* so the compiler computes
    // the correct base-class offset.
    if (tag_prefix() >= kTagSymbol)
      return static_cast<RefCounted *>(reinterpret_cast<GCObjectHeader *>(raw));
    // RC-only (0x7FF0–0x7FF2): String, VarRef, StringRope — no vtable.
    return reinterpret_cast<RefCounted *>(raw);
  }

  template <typename T>
  T *as() const {
    return static_cast<T *>(as_pointer());
  }

  double as_double() const {
    double d;
    std::memcpy(&d, &data, sizeof(d));
    return d;
  }
};
} // namespace qjsp
