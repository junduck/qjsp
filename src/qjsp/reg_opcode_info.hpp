#pragma once

#include "qjsp/reg_opcode.hpp"

namespace qjsp {

inline constexpr RegOpInfo kRegOpInfo[] = {
    {"INVALID", InstrFmt::ABx},

    // ── padding ─────────────────────────────────────────────────────────────
    {"NOP", InstrFmt::ABx},

    // ── const / move (9) ────────────────────────────────────────────────────
    {"LOADK", InstrFmt::ABx},
    {"LOADINT", InstrFmt::AsBx},
    {"LOADNIL", InstrFmt::ABC},
    {"LOADBOOL", InstrFmt::ABC},
    {"LOADUNDEF", InstrFmt::ABx},
    {"LOADNULL", InstrFmt::ABx},
    {"LOADTRUE", InstrFmt::ABx},
    {"LOADFALSE", InstrFmt::ABx},
    {"MOVE", InstrFmt::ABC},

    // ── arithmetic (6) ──────────────────────────────────────────────────────
    {"ADD", InstrFmt::ABC},
    {"SUB", InstrFmt::ABC},
    {"MUL", InstrFmt::ABC},
    {"DIV", InstrFmt::ABC},
    {"MOD", InstrFmt::ABC},
    {"POW", InstrFmt::ABC},

    // ── bitwise (6) ─────────────────────────────────────────────────────────
    {"AND", InstrFmt::ABC},
    {"OR", InstrFmt::ABC},
    {"XOR", InstrFmt::ABC},
    {"SHL", InstrFmt::ABC},
    {"SAR", InstrFmt::ABC},
    {"SHR", InstrFmt::ABC},

    // ── unary (5) ───────────────────────────────────────────────────────────
    {"NEG", InstrFmt::ABC},
    {"BNOT", InstrFmt::ABC},
    {"LNOT", InstrFmt::ABC},
    {"INC", InstrFmt::ABC},
    {"DEC", InstrFmt::ABC},

    // ── compare (8) ─────────────────────────────────────────────────────────
    {"EQ", InstrFmt::ABC},
    {"NEQ", InstrFmt::ABC},
    {"SEQ", InstrFmt::ABC},
    {"SNEQ", InstrFmt::ABC},
    {"LT", InstrFmt::ABC},
    {"GT", InstrFmt::ABC},
    {"LTE", InstrFmt::ABC},
    {"GTE", InstrFmt::ABC},

    // ── control (4) ─────────────────────────────────────────────────────────
    {"JMP", InstrFmt::AsBx},
    {"IS_FALSE", InstrFmt::AsBx},
    {"IS_TRUE", InstrFmt::AsBx},
    {"IS_UNDEF", InstrFmt::AsBx},

    // ── object (7) ──────────────────────────────────────────────────────────
    {"NEWOBJ", InstrFmt::ABx},
    {"GETFIELD", InstrFmt::ABC},
    {"SETFIELD", InstrFmt::ABC},
    {"DEFINE_FIELD", InstrFmt::ABC},
    {"GETELEM", InstrFmt::ABC},
    {"SETELEM", InstrFmt::ABC},
    {"DEFINE_ELEM", InstrFmt::ABC},

    // ── array (2) ───────────────────────────────────────────────────────────
    {"NEWARR", InstrFmt::ABC},
    {"APPEND", InstrFmt::ABC},

    // ── spread (2) ──────────────────────────────────────────────────────────
    {"SPREAD_OBJ", InstrFmt::ABC},
    {"CALL_SPREAD", InstrFmt::ABC},

    // ── type / conversion (4) ───────────────────────────────────────────────
    {"TYPEOF", InstrFmt::ABC},
    {"TOPROPKEY", InstrFmt::ABC},
    {"SETPROTO", InstrFmt::ABC},
    {"TOOBJECT", InstrFmt::ABC},

    // ── call / return (6) ───────────────────────────────────────────────────
    {"CALL", InstrFmt::ABC},
    {"CALL_M", InstrFmt::ABC},
    {"CTOR", InstrFmt::ABC},
    {"FCLOSURE", InstrFmt::ABx},
    {"RETURN", InstrFmt::ABC},
    {"RETURN0", InstrFmt::ABx},
    {"THROW", InstrFmt::ABC},
    {"CATCH", InstrFmt::ABx}, //  A=exc_reg, Bx=catch_pc (patched)
    {"UNCATCH", InstrFmt::ABx},
    {"GOSUB", InstrFmt::AsBx}, //  A unused, sbx=relative offset
    {"RET", InstrFmt::ABx},

    // ── upvalue (3) ─────────────────────────────────────────────────────────
    {"GETUPVAL", InstrFmt::ABC},
    {"SETUPVAL", InstrFmt::ABC},
    {"CLOSEUPVAL", InstrFmt::ABx},

    // ── iteration (4) ────────────────────────────────────────────────────────
    {"FOR_IN_START", InstrFmt::ABC}, //  A=iter_reg, B=obj_reg
    {"FOR_IN_NEXT", InstrFmt::ABC},  //  A=key_reg, B=iter_reg, C=more_reg
    {"FOR_OF_START", InstrFmt::ABC}, //  A=iter_reg, B=obj_reg
    {"FOR_OF_NEXT", InstrFmt::ABC},  //  A=val_reg, B=iter_reg, C=more_reg

    // ── regexp (1) ─────────────────────────────────────────────────────────
    {"REGEXP", InstrFmt::ABx}, //  A=dst, Bx=cpool_idx (pattern; flags at idx+1)

    // ── instanceof (1) ─────────────────────────────────────────────────────
    {"INSTANCEOF", InstrFmt::ABC}, //  A=result, B=obj_reg, C=ctor_reg
};

static_assert(sizeof(kRegOpInfo) / sizeof(kRegOpInfo[0]) == static_cast<int>(RegOp::NUM_OPCODES), "kRegOpInfo size mismatch");

inline uint8_t op(RegOp o) { return static_cast<uint8_t>(o); }

} // namespace qjsp
