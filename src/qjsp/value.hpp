//! NaN-boxed JavaScript value (64-bit).
//!
//! All tagged values use the quiet NaN space (top 13 bits = 0x7FF8+).
//! Bit 51 (quiet bit) is always set, avoiding signaling NaN traps on ARM64.
//!
//! Tag layout (prefix in bits 48-63):
//!
//! | Type of           | Bit Layout | Comment |
//! |-------------------|------------|---------|
//! | `NAN` (quiet)     | `7FF8:0000:0000:0000`    | |
//! | `Symbol`          | `7FF8:IIII:IIII:IIII`    | Non-zero uint payload |
//!   ─────── pointer boundary ─────── RC (RefCounted only, no vtable)
//! | `RC`              | `7FF9:PPPP:PPPP:PPPP`    | 8bit alignment pointer, lower 3 bits type descrimination |
//! | `RESERVED`        | `7FFA:PPPP:PPPP:PPPP`    | not used |
//! | `RESERVED`        | `7FFB:PPPP:PPPP:PPPP`    | not used |
//!   ─────── pointer boundary ─────── GC (GCObjectHeader subclasses)
//! | `Object`          | `7FFC:PPPP:PPPP:PPPP`    | |
//! | `RESERVED`        | `7FFD:PPPP:PPPP:PPPP`    | |
//! | `RESERVED`        | `7FFE:0000:0000:0000`    | not used |
//!   ─────── scalar boundary ─────── 7FFF - [type] - [payload]
//! | `ShortBigInt`     | `7FFF:0000:IIII:IIII`        | int32_t |
//! | `Bool`            | `7FFF:0001:0000:000[0,1]`    | 0 false 1 true |
//! | `Null/Undef`      | `7FFF:0002:0000:000[0,1]`    | 0 null 1 undef |
//! | `Exception`       | `7FFF:0003:0000:0000`    | |
//! | `Uninitialized`   | `7FFF:FFFF:FFFF:FFFF`    | |
//! | `Float64`         | Any other values.        | |
//!
//! Pointer checks:      t in [kPtrStart, kPtrEnd] (0x7FF9–0x7FFC)
//! Double checks:       t < kTagSymbol || (t > kPtrEnd && t < kTagScalar) || t > kTaggedMax ||
//!                      data == kCanonicalNaN
//! RC-only (no vtable): tag_prefix() == kTagRC (0x7FF9), sub-type in lower 3 bits
//! GC-object (vtable):  tag_prefix() == kTagObject  (0x7FFC)
//!
//! Symbol: scalar tag 0x7FF8, lower 48 bits = Atom index (non-zero).
//! kAtomNull (0) is never used as a symbol, so 0x7FF8_0000_0000_0000
//! unambiguously means double quiet NaN.

//! RC sub types:
//! 0 StrPrim
//! 1 VarRef
//! 2 Bytecode
//! 3 BigInt
//!
//! Obj sub types:
//! 0 Object   (plain)
//! 1 Callable (CFunctionObj / BFunctionObj)  — is_callable() returns true

#pragma once

#include "qjsp/atom.hpp"
#include "qjsp/gc.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

namespace qjsp {

struct VarRef;
struct Bytecode;

constexpr int kTagShift         = 48;
constexpr uint64_t kTagMask     = 0xFFFFull << kTagShift;
constexpr uint64_t kPayloadMask = (1ull << kTagShift) - 1;

// ─── Scalar tag (0x7FF8): payload = Atom index (non-zero) ──────────────
constexpr uint64_t kTagSymbol = 0x7FF8ull;

// ─── Pointer tags ─── RC (0x7FF9): RefCounted only, no vtable ──────
// Sub-types stored in lower 3 bits of 8-byte-aligned pointer payload:
//   0 = StrPrim, 1 = VarRef, 2 = Bytecode, 3 = BigInt
constexpr uint64_t kTagRC = 0x7FF9ull;

// ─── Pointer tags ─── GC (0x7FFC): GCObjectHeader subclasses ─────────────
constexpr uint64_t kTagObject = 0x7FFCull;

// ─── Pointer sub-type (stored in lower 3 bits of 8-byte-aligned pointer) ──
constexpr uintptr_t kPtrSubMask = 0x7ull;
enum class RCType : uintptr_t {
  StrPrim  = 0,
  VarRef   = 1,
  Bytecode = 2,
  BigInt   = 3,
};
enum class ObjType : uintptr_t {
  Object   = 0,
  Callable = 1,
};

// ─── Canonical quiet NaN ───────────────────────────────────────────────────
constexpr uint64_t kCanonicalNaN = kTagSymbol << kTagShift; // payload = 0

// ─── Scalar family (0x7FFF): subtype in bits 32-47 ────────────────────────
constexpr uint64_t kTagScalar         = 0x7FFFull;
constexpr uint32_t kScalarShortBigInt = 0x0000;
constexpr uint32_t kScalarBool        = 0x0001;
constexpr uint32_t kScalarNullUndef   = 0x0002;
constexpr uint32_t kScalarException   = 0x0003;
// Uninitialized uses subtype 0xFFFF with payload 0xFFFFFFFF

// ─── Derived boundaries ────────────────────────────────────────────────────
constexpr uint64_t kPtrStart  = kTagRC;     // first pointer tag (0x7FF9)
constexpr uint64_t kPtrEnd    = kTagObject; // last pointer tag (0x7FFC)
constexpr uint64_t kTaggedMin = kTagSymbol; // smallest tag in NaN-box range (0x7FF8)
constexpr uint64_t kTaggedMax = kTagScalar; // largest tag overall (0x7FFF)

// ─── Value ─────────────────────────────────────────────────────────────────

struct Value {
  uint64_t data;

  constexpr Value() : data(kTagScalar << kTagShift | (static_cast<uint64_t>(kScalarNullUndef) << 32)) {}
  explicit constexpr Value(uint64_t d) : data(d) {}

  // ── RAII ─────────────────────────────────────────────────────────────

  Value(Value const &other) noexcept : data(other.data) {
    if (is_pointer() && !is_nullptr())
      get_ref_counted()->ref();
  }

  Value(Value &&other) noexcept : data(other.data) { other.data = kTagScalar << kTagShift | (static_cast<uint64_t>(kScalarNullUndef) << 32) | 1ull; }

  Value &operator=(Value other) noexcept {
    swap(other);
    return *this;
  }

  void swap(Value &other) noexcept {
    using std::swap;
    swap(data, other.data);
  }

  ~Value();

  // ── scalar helpers ───────────────────────────────────────────────────

  uint32_t scalar_type() const { return static_cast<uint32_t>((data >> 32) & 0xFFFF); }

  // ── factories (scalar family, tag 0x7FFF) ────────────────────────────

  static constexpr Value int32(int32_t v) {
    return Value{(kTagScalar << kTagShift) | (static_cast<uint64_t>(kScalarShortBigInt) << 32) | (static_cast<uint64_t>(static_cast<uint32_t>(v)))};
  }

  static constexpr Value bool_(bool v) { return Value{(kTagScalar << kTagShift) | (static_cast<uint64_t>(kScalarBool) << 32) | (v ? 1ull : 0ull)}; }

  static constexpr Value null_() { return Value{(kTagScalar << kTagShift) | (static_cast<uint64_t>(kScalarNullUndef) << 32)}; }

  static constexpr Value undefined_() { return Value{(kTagScalar << kTagShift) | (static_cast<uint64_t>(kScalarNullUndef) << 32) | 1ull}; }

  static constexpr Value exception() { return Value{(kTagScalar << kTagShift) | (static_cast<uint64_t>(kScalarException) << 32)}; }

  static constexpr Value uninitialized() { return Value{(kTagScalar << kTagShift) | 0xFFFFFFFFFFFFFFFFull}; }

  // ── factories (symbol, scalar tag 0x7FF8) ────────────────────────────

  static constexpr Value symbol_from_atom(Atom a) { return Value{(kTagSymbol << kTagShift) | static_cast<uint64_t>(a)}; }

  // ── factories (pointer) ──────────────────────────────────────────────

  static Value object(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    assert((p & kPtrSubMask) == 0);
    return Value{(kTagObject << kTagShift) | p | static_cast<uintptr_t>(ObjType::Object)};
  }

  static Value callable(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    assert((p & kPtrSubMask) == 0);
    return Value{(kTagObject << kTagShift) | p | static_cast<uintptr_t>(ObjType::Callable)};
  }

  static Value var_ref(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    assert((p & kPtrSubMask) == 0);
    return Value{(kTagRC << kTagShift) | p | static_cast<uintptr_t>(RCType::VarRef)};
  }

  static Value string(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    assert((p & kPtrSubMask) == 0);
    return Value{(kTagRC << kTagShift) | p | static_cast<uintptr_t>(RCType::StrPrim)};
  }

  static Value bigint_ptr(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    assert((p & kPtrSubMask) == 0);
    return Value{(kTagRC << kTagShift) | p | static_cast<uintptr_t>(RCType::BigInt)};
  }

  static Value bytecode(void *ptr) {
    auto p = reinterpret_cast<uintptr_t>(ptr);
    assert((p & kTagMask) == 0);
    assert((p & kPtrSubMask) == 0);
    return Value{(kTagRC << kTagShift) | p | static_cast<uintptr_t>(RCType::Bytecode)};
  }

  // ── legacy alias ─────────────────────────────────────────────────────
  static Value func_bytecode(void *ptr) { return bytecode(ptr); }

  // ── float64 ──────────────────────────────────────────────────────────

  static Value float64(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    // Canonicalize doubles that fall in our tagged NaN space.
    // QNaN (0x7FF8000000000000) is kept — it has tag=kTagSymbol with payload=0.
    // Since kAtomNull is never used as a Symbol, this pattern remains QNaN.
    if (bits == kCanonicalNaN)
      return Value{bits};
    uint16_t t = static_cast<uint16_t>(bits >> kTagShift);
    if (t >= kTaggedMin && t <= kTaggedMax)
      bits = kCanonicalNaN;
    return Value{bits};
  }

  // ── type queries ─────────────────────────────────────────────────────

  uint16_t tag_prefix() const { return static_cast<uint16_t>(data >> kTagShift); }

  // scalar family (tag 0x7FFF)
  bool is_uninitialized() const { return data == uninitialized().data; }
  bool is_exception() const { return tag_prefix() == kTagScalar && scalar_type() == kScalarException; }
  bool is_int32() const { return tag_prefix() == kTagScalar && scalar_type() == kScalarShortBigInt; }
  bool is_bool() const { return tag_prefix() == kTagScalar && scalar_type() == kScalarBool; }
  bool is_null() const { return tag_prefix() == kTagScalar && scalar_type() == kScalarNullUndef && (data & 1) == 0; }
  bool is_undefined() const { return tag_prefix() == kTagScalar && scalar_type() == kScalarNullUndef && (data & 1) == 1; }
  bool is_null_or_undef() const { return tag_prefix() == kTagScalar && scalar_type() == kScalarNullUndef; }

  // pointer types
  bool is_rc() const { return tag_prefix() == kTagRC; }
  bool is_object() const { return tag_prefix() == kTagObject; } // matches Object + Callable
  bool is_callable() const { return tag_prefix() == kTagObject && obj_type() == ObjType::Callable; }
  bool is_string() const { return is_rc() && rc_type() == RCType::StrPrim && !is_nullptr(); }
  bool is_bigint_ptr() const { return is_rc() && rc_type() == RCType::BigInt; }
  bool is_var_ref() const { return is_rc() && rc_type() == RCType::VarRef; }
  bool is_bytecode() const { return is_rc() && rc_type() == RCType::Bytecode; }

  bool is_symbol() const { return tag_prefix() == kTagSymbol && (data & kPayloadMask) != 0; }

  bool is_pointer() const {
    uint16_t t = tag_prefix();
    return t >= kPtrStart && t <= kPtrEnd;
  }

  bool is_nullptr() const { return (data & kPayloadMask & ~kPtrSubMask) == 0; }

  bool is_double() const {
    // QNaN (0x7FF8000000000000) is the only double with tag in our range.
    // Symbol payload is always non-zero.
    if (data == kCanonicalNaN)
      return true;
    uint16_t t = tag_prefix();
    return t < kTaggedMin || t > kTaggedMax;
  }

  bool is_number() const { return is_double() || is_int32(); }
  bool is_bigint() const { return is_int32() || is_bigint_ptr(); }

  // ── ToBoolean coercion ────────────────────────────────────────────────

  bool is_truthy() const {
    if (is_null() || is_undefined()) return false;
    if (is_bool()) return as_bool();
    if (is_int32()) return as_int32() != 0;
    if (is_double()) return as_double() != 0.0;
    return true; // objects, strings, symbols — all truthy
  }
  bool is_falsy() const { return !is_truthy(); }

  // ── extractors ───────────────────────────────────────────────────────

  RCType rc_type() const { return static_cast<RCType>(data & kPtrSubMask); }
  ObjType obj_type() const { return static_cast<ObjType>(data & kPtrSubMask); }

  int32_t as_int32() const { return static_cast<int32_t>(data & 0xFFFFFFFFu); }
  Atom as_symbol() const { return static_cast<Atom>(data & 0xFFFFFFFFu); }
  bool as_bool() const { return (data & 1) != 0; }

  uintptr_t raw_ptr() const { return static_cast<uintptr_t>(data & kPayloadMask) & ~kPtrSubMask; }

  void *as_pointer() const { return reinterpret_cast<void *>(raw_ptr()); }

  RefCounted *get_ref_counted() const {
    uintptr_t raw = raw_ptr();
    if (raw == 0)
      return nullptr;
    // GC-objects (0x7FFC): Object — GCObjectHeader subclass with vtable.
    if (tag_prefix() == kTagObject)
      return static_cast<RefCounted *>(reinterpret_cast<GCObjectHeader *>(raw));
    // RC-only (0x7FF9): VarRef, Bytecode, StrPrim, BigInt — no vtable.
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
