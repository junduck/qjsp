#pragma once

#include <cstdint>

namespace qjsp {

// ─── Encoding formats ───────────────────────────────────────────────────────
//
//   iABC:  [opcode:8][A:8][B:8][C:8]
//   iABx:  [opcode:8][A:8][Bx:16]
//   iAsBx: [opcode:8][A:8][sBx:16]    (signed 16-bit)

enum class InstrFmt : uint8_t { ABC, ABx, AsBx };

// ─── Instruction ────────────────────────────────────────────────────────────

struct Instruction {
  uint32_t raw;

  constexpr Instruction() : raw(0) {}
  explicit constexpr Instruction(uint32_t r) : raw(r) {}

  // ── decoders ──────────────────────────────────────────────────────────

  uint8_t opcode() const { return static_cast<uint8_t>(raw & 0xFFu); }
  uint8_t a() const { return static_cast<uint8_t>((raw >> 8) & 0xFFu); }
  uint8_t b() const { return static_cast<uint8_t>((raw >> 16) & 0xFFu); }
  uint8_t c() const { return static_cast<uint8_t>((raw >> 24) & 0xFFu); }
  uint16_t bx() const { return static_cast<uint16_t>((raw >> 16) & 0xFFFFu); }
  int16_t sbx() const { return static_cast<int16_t>((raw >> 16) & 0xFFFFu); }

  // ── encoders ──────────────────────────────────────────────────────────

  static constexpr Instruction iABC(uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    return Instruction{static_cast<uint32_t>(op) | (static_cast<uint32_t>(a) << 8) | (static_cast<uint32_t>(b) << 16) |
                       (static_cast<uint32_t>(c) << 24)};
  }

  static constexpr Instruction iABx(uint8_t op, uint8_t a, uint16_t bx) {
    return Instruction{static_cast<uint32_t>(op) | (static_cast<uint32_t>(a) << 8) | (static_cast<uint32_t>(bx) << 16)};
  }

  static constexpr Instruction iAsBx(uint8_t op, uint8_t a, int16_t sbx) {
    return Instruction{static_cast<uint32_t>(op) | (static_cast<uint32_t>(a) << 8) | (static_cast<uint32_t>(static_cast<uint16_t>(sbx)) << 16)};
  }
};

// ─── Opcode enum ────────────────────────────────────────────────────────────

enum class RegOp : uint8_t {
  // ── sentinel ──────────────────────────────────────────────────────────
  INVALID = 0,

  // ── padding ───────────────────────────────────────────────────────────
  NOP,

  // ── const / move (9) ──────────────────────────────────────────────────
  LOADK,
  LOADINT,
  LOADNIL,
  LOADBOOL,
  LOADUNDEF,
  LOADNULL,
  LOADTRUE,
  LOADFALSE,
  MOVE,

  // ── arithmetic (6) ────────────────────────────────────────────────────
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  POW,

  // ── bitwise (6) ───────────────────────────────────────────────────────
  AND,
  OR,
  XOR,
  SHL,
  SAR,
  SHR,

  // ── unary (5) ─────────────────────────────────────────────────────────
  NEG,
  BNOT,
  LNOT,
  INC,
  DEC,

  // ── compare (8) ───────────────────────────────────────────────────────
  EQ,
  NEQ,
  SEQ,
  SNEQ,
  LT,
  GT,
  LTE,
  GTE,

  // ── control (3, + NOP above) ─────────────────────────────────────────
  JMP,
  IS_FALSE,
  IS_TRUE,

  // ── object (7) ────────────────────────────────────────────────────────
  NEWOBJ,
  GETFIELD,
  SETFIELD,
  DEFINE_FIELD,
  GETELEM,
  SETELEM,
  DEFINE_ELEM,

  // ── array (2) ─────────────────────────────────────────────────────────
  NEWARR,
  APPEND,

  // ── spread (2) ────────────────────────────────────────────────────────
  SPREAD_OBJ,
  CALL_SPREAD,

  // ── type / conversion (4) ─────────────────────────────────────────────
  TYPEOF,
  TOPROPKEY,
  SETPROTO,
  TOOBJECT,

  // ── call / return (6) ─────────────────────────────────────────────────
  CALL,
  CALL_M,
  CTOR,
  FCLOSURE,
  RETURN,
  RETURN0,
  THROW,
  CATCH,
  UNCATCH,
  GOSUB,
  RET,

  // ── upvalue (3) ───────────────────────────────────────────────────────
  GETUPVAL,
  SETUPVAL,
  CLOSEUPVAL,

  // ── iteration (4) ──────────────────────────────────────────────────────
  FOR_IN_START,
  FOR_IN_NEXT,
  FOR_OF_START,
  FOR_OF_NEXT,

  NUM_OPCODES // sentinel count
};

// ─── Opcode info ────────────────────────────────────────────────────────────

struct RegOpInfo {
  const char *name;
  InstrFmt fmt;
};

extern const RegOpInfo kRegOpInfo[];

} // namespace qjsp
