#pragma once

#include "qjsp/reg_opcode.hpp"

namespace qjsp {

inline constexpr RegOpInfo kRegOpInfo[] = {
    {"INVALID", InstrFmt::ABx}, // 0

    // ── padding ─────────────────────────────────────────────────────────────
    {"NOP", InstrFmt::ABx}, // 1

    // ── const / move (9) ────────────────────────────────────────────────────
    {"LOADK", InstrFmt::ABx},     // 2
    {"LOADINT", InstrFmt::AsBx},  // 3
    {"LOADNIL", InstrFmt::ABC},   // 4
    {"LOADBOOL", InstrFmt::ABC},  // 5
    {"LOADUNDEF", InstrFmt::ABx}, // 6
    {"LOADNULL", InstrFmt::ABx},  // 7
    {"LOADTRUE", InstrFmt::ABx},  // 8
    {"LOADFALSE", InstrFmt::ABx}, // 9
    {"MOVE", InstrFmt::ABC},      // 10

    // ── arithmetic (6) ──────────────────────────────────────────────────────
    {"ADD", InstrFmt::ABC}, // 11
    {"SUB", InstrFmt::ABC}, // 12
    {"MUL", InstrFmt::ABC}, // 13
    {"DIV", InstrFmt::ABC}, // 14
    {"MOD", InstrFmt::ABC}, // 15
    {"POW", InstrFmt::ABC}, // 16

    // ── bitwise (6) ─────────────────────────────────────────────────────────
    {"AND", InstrFmt::ABC}, // 17
    {"OR", InstrFmt::ABC},  // 18
    {"XOR", InstrFmt::ABC}, // 19
    {"SHL", InstrFmt::ABC}, // 20
    {"SAR", InstrFmt::ABC}, // 21
    {"SHR", InstrFmt::ABC}, // 22

    // ── unary (5) ───────────────────────────────────────────────────────────
    {"NEG", InstrFmt::ABC},  // 23
    {"BNOT", InstrFmt::ABC}, // 24
    {"LNOT", InstrFmt::ABC}, // 25
    {"INC", InstrFmt::ABC},  // 26
    {"DEC", InstrFmt::ABC},  // 27

    // ── compare (8) ─────────────────────────────────────────────────────────
    {"EQ", InstrFmt::ABC},   // 28
    {"NEQ", InstrFmt::ABC},  // 29
    {"SEQ", InstrFmt::ABC},  // 30
    {"SNEQ", InstrFmt::ABC}, // 31
    {"LT", InstrFmt::ABC},   // 32
    {"GT", InstrFmt::ABC},   // 33
    {"LTE", InstrFmt::ABC},  // 34
    {"GTE", InstrFmt::ABC},  // 35

    // ── control (3) ─────────────────────────────────────────────────────────
    {"JMP", InstrFmt::AsBx},      // 36
    {"IS_FALSE", InstrFmt::AsBx}, // 37
    {"IS_TRUE", InstrFmt::AsBx},  // 38

    // ── object (7) ──────────────────────────────────────────────────────────
    {"NEWOBJ", InstrFmt::ABx},       // 39
    {"GETFIELD", InstrFmt::ABC},     // 40
    {"SETFIELD", InstrFmt::ABC},     // 41
    {"DEFINE_FIELD", InstrFmt::ABC}, // 42
    {"GETELEM", InstrFmt::ABC},      // 43
    {"SETELEM", InstrFmt::ABC},      // 44
    {"DEFINE_ELEM", InstrFmt::ABC},  // 45

    // ── array (2) ───────────────────────────────────────────────────────────
    {"NEWARR", InstrFmt::ABC}, // 46
    {"APPEND", InstrFmt::ABC}, // 47

    // ── spread (2) ──────────────────────────────────────────────────────────
    {"SPREAD_OBJ", InstrFmt::ABC},  // 48
    {"CALL_SPREAD", InstrFmt::ABC}, // 49

    // ── type / conversion (4) ───────────────────────────────────────────────
    {"TYPEOF", InstrFmt::ABC},    // 50
    {"TOPROPKEY", InstrFmt::ABC}, // 51
    {"SETPROTO", InstrFmt::ABC},  // 52
    {"TOOBJECT", InstrFmt::ABC},  // 53

    // ── call / return (6) ───────────────────────────────────────────────────
    {"CALL", InstrFmt::ABC},     // 54
    {"CALL_M", InstrFmt::ABC},   // 55
    {"CTOR", InstrFmt::ABC},     // 56
    {"FCLOSURE", InstrFmt::ABx}, // 57
    {"RETURN", InstrFmt::ABC},   // 58
    {"RETURN0",       InstrFmt::ABx},   // 59
    {"THROW",         InstrFmt::ABC},   // 60
    {"CATCH",         InstrFmt::ABx},   // 61  A=exc_reg, Bx=catch_pc (patched)
    {"UNCATCH",       InstrFmt::ABx},   // 62
    {"GOSUB",         InstrFmt::AsBx},  // 63  A unused, sbx=relative offset
    {"RET",           InstrFmt::ABx},   // 64

    // ── upvalue (3) ─────────────────────────────────────────────────────────
    {"GETUPVAL",      InstrFmt::ABC},   // 65
    {"SETUPVAL",      InstrFmt::ABC},   // 66
    {"CLOSEUPVAL",    InstrFmt::ABx},   // 67
};

static_assert(sizeof(kRegOpInfo) / sizeof(kRegOpInfo[0]) == static_cast<int>(RegOp::NUM_OPCODES), "kRegOpInfo size mismatch");

inline uint8_t op(RegOp o) { return static_cast<uint8_t>(o); }

} // namespace qjsp
