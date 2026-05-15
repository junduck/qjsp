#pragma once

#include <cstdint>

namespace qjsp {

// ─── Bit masks for packed TokenTag ──────────────────────────────────────────

namespace TM {
constexpr uint32_t Numeric    = 1u << 13;
constexpr uint32_t BinaryOp   = 1u << 14;
constexpr uint32_t LogicalOp  = 1u << 15;
constexpr uint32_t UnaryOp    = 1u << 16;
constexpr uint32_t AssignOp   = 1u << 17;
constexpr uint32_t IdentLike  = 1u << 18;
constexpr uint32_t Reserved   = 1u << 19;
constexpr uint32_t Keyword    = 1u << 20;
constexpr uint32_t PrecShift  = 8;
constexpr uint32_t PrecMask   = 0x1Fu;   // 5 bits
}

// ─── TokenTag — bit-packed token discriminant ───────────────────────────────
//
// Bits 0–7:   unique ID
// Bits 8–12:  operator precedence (0–31)
// Bit  13:    is numeric literal
// Bit  14:    is binary operator
// Bit  15:    is logical operator
// Bit  16:    is unary operator
// Bit  17:    is assignment operator
// Bit  18:    is identifier-like (identifiers + keywords + literal keywords)
// Bit  19:    is reserved (unconditionally — always reserved in strict mode)
// Bit  20:    is keyword
//
// QJSP is strict-mode-only, so IsReserved covers everything that's reserved
// in strict mode (no need for a separate IsStrictModeReserved bit).

enum TokenTag : uint32_t {
    // ── Literals ────────────────────────────────────────────────────────────
    tok_numeric     = 1  | TM::Numeric,
    tok_hex         = 2  | TM::Numeric,
    tok_octal       = 3  | TM::Numeric,
    tok_binary      = 4  | TM::Numeric,
    tok_regexp      = 5,
    tok_template_full = 6 | (17u << TM::PrecShift),
    tok_template_head = 7 | (17u << TM::PrecShift),
    tok_template_mid  = 8,
    tok_template_tail = 9,
    tok_bigint      = 10 | TM::Numeric,

    tok_string      = 11,
    tok_hashbang    = 12,
    // ── Keyword literals ────────────────────────────────────────────────────
    tok_null   = 13 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_true   = 14 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_false  = 15 | TM::Keyword | TM::Reserved | TM::IdentLike,

    // ── Arithmetic ──────────────────────────────────────────────────────────
    tok_plus    = 15 | (12u << TM::PrecShift) | TM::BinaryOp | TM::UnaryOp,
    tok_minus   = 16 | (12u << TM::PrecShift) | TM::BinaryOp | TM::UnaryOp,
    tok_star    = 17 | (13u << TM::PrecShift) | TM::BinaryOp,
    tok_slash   = 18 | (13u << TM::PrecShift) | TM::BinaryOp,
    tok_percent = 19 | (13u << TM::PrecShift) | TM::BinaryOp,
    tok_pow     = 20 | (14u << TM::PrecShift) | TM::BinaryOp,

    // ── Assignment ──────────────────────────────────────────────────────────
    tok_assign       = 21 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_plus_assign  = 22 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_minus_assign = 23 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_star_assign  = 24 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_slash_assign = 25 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_pct_assign   = 26 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_pow_assign   = 27 | (2u << TM::PrecShift) | TM::AssignOp,

    // ── Update ──────────────────────────────────────────────────────────────
    tok_inc = 28 | (15u << TM::PrecShift),
    tok_dec = 29 | (15u << TM::PrecShift),

    // ── Equality ────────────────────────────────────────────────────────────
    tok_eq   = 30 | (9u << TM::PrecShift) | TM::BinaryOp,
    tok_neq  = 31 | (9u << TM::PrecShift) | TM::BinaryOp,
    tok_seq  = 32 | (9u << TM::PrecShift) | TM::BinaryOp,
    tok_sneq = 33 | (9u << TM::PrecShift) | TM::BinaryOp,

    // ── Relational ──────────────────────────────────────────────────────────
    tok_lt         = 34 | (10u << TM::PrecShift) | TM::BinaryOp,
    tok_gt         = 35 | (10u << TM::PrecShift) | TM::BinaryOp,
    tok_lte        = 36 | (10u << TM::PrecShift) | TM::BinaryOp,
    tok_gte        = 37 | (10u << TM::PrecShift) | TM::BinaryOp,
    tok_instanceof = 38 | (10u << TM::PrecShift) | TM::BinaryOp | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_in         = 39 | (10u << TM::PrecShift) | TM::BinaryOp | TM::Keyword | TM::Reserved | TM::IdentLike,

    // ── Logical ─────────────────────────────────────────────────────────────
    tok_land  = 40 | (4u << TM::PrecShift) | TM::LogicalOp,
    tok_lor   = 41 | (3u << TM::PrecShift) | TM::LogicalOp,
    tok_nullish = 42 | (3u << TM::PrecShift) | TM::LogicalOp,
    tok_bang  = 43 | (15u << TM::PrecShift) | TM::UnaryOp,

    // ── Bitwise ─────────────────────────────────────────────────────────────
    tok_band  = 44 | (7u << TM::PrecShift) | TM::BinaryOp,
    tok_bor   = 45 | (5u << TM::PrecShift) | TM::BinaryOp,
    tok_bxor  = 46 | (6u << TM::PrecShift) | TM::BinaryOp,
    tok_bnot  = 47 | TM::UnaryOp,
    tok_shl   = 48 | (11u << TM::PrecShift) | TM::BinaryOp,
    tok_sar   = 49 | (11u << TM::PrecShift) | TM::BinaryOp,
    tok_shr   = 50 | (11u << TM::PrecShift) | TM::BinaryOp,

    // ── Compound bitwise assignment ─────────────────────────────────────────
    tok_band_assign = 51 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_bor_assign  = 52 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_bxor_assign = 53 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_shl_assign  = 54 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_sar_assign  = 55 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_shr_assign  = 56 | (2u << TM::PrecShift) | TM::AssignOp,

    // ── Nullish / optional / logical assign ─────────────────────────────────
    tok_nullish_assign = 57 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_land_assign    = 58 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_lor_assign     = 59 | (2u << TM::PrecShift) | TM::AssignOp,
    tok_opt_chain      = 60 | (17u << TM::PrecShift),

    // ── Punctuation ─────────────────────────────────────────────────────────
    tok_lparen  = 61 | (17u << TM::PrecShift),
    tok_rparen  = 62,
    tok_lbrace  = 63,
    tok_rbrace  = 64,
    tok_lbrack  = 65 | (17u << TM::PrecShift),
    tok_rbrack  = 66,
    tok_semi    = 67,
    tok_comma   = 68 | (1u << TM::PrecShift),
    tok_dot     = 69 | (17u << TM::PrecShift),
    tok_spread  = 70,
    tok_arrow   = 71 | (2u << TM::PrecShift),
    tok_question = 72 | (3u << TM::PrecShift),
    tok_colon   = 73,

    // ── Keywords — always reserved ──────────────────────────────────────────
    tok_if         = 80 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_else       = 81 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_switch     = 82 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_case       = 83 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_default    = 84 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_for        = 85 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_while      = 86 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_do         = 87 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_break      = 88 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_continue   = 89 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_function   = 90 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_return     = 91 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_var        = 92 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_const      = 93 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_class      = 94 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_extends    = 95 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_super      = 96 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_try        = 97 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_catch      = 98 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_finally    = 99 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_throw      = 100 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_new        = 101 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_this       = 102 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_typeof     = 103 | (15u << TM::PrecShift) | TM::UnaryOp | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_delete     = 104 | (15u << TM::PrecShift) | TM::UnaryOp | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_void       = 105 | (15u << TM::PrecShift) | TM::UnaryOp | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_with       = 106 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_debugger   = 107 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_enum       = 108 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_export     = 109 | TM::Keyword | TM::Reserved,
    tok_import     = 110 | TM::Keyword | TM::Reserved,

    // ── Keywords — reserved in strict mode (all of them, since QJSP is strict-only)
    tok_let        = 120 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_static     = 121 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_yield      = 122 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_await      = 123 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_implements = 124 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_interface  = 125 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_package    = 126 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_private    = 127 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_protected  = 128 | TM::Keyword | TM::Reserved | TM::IdentLike,
    tok_public     = 129 | TM::Keyword | TM::Reserved | TM::IdentLike,

    // ── Contextual keywords ─────────────────────────────────────────────────
    tok_of       = 140 | TM::IdentLike,
    tok_async    = 141 | TM::IdentLike,
    tok_from     = 142 | TM::IdentLike,
    tok_as       = 143 | TM::IdentLike,
    tok_get      = 144 | TM::IdentLike,
    tok_set      = 145 | TM::IdentLike,
    tok_constructor = 146 | TM::IdentLike,

    // ── Identifier / private ────────────────────────────────────────────────
    tok_ident        = 150 | TM::IdentLike,
    tok_private_ident = 151 | TM::IdentLike,

    // ── EOF ──────────────────────────────────────────────────────────────────
    tok_eof = 0,
};

// ─── TokenTag query functions ────────────────────────────────────────────────

inline uint8_t  tag_prec(TokenTag t) { return (static_cast<uint32_t>(t) >> TM::PrecShift) & TM::PrecMask; }
inline bool     tag_has(TokenTag t, uint32_t mask) { return (static_cast<uint32_t>(t) & mask) != 0; }
inline bool     tag_is_numeric(TokenTag t)   { return tag_has(t, TM::Numeric); }
inline bool     tag_is_binary(TokenTag t)    { return tag_has(t, TM::BinaryOp); }
inline bool     tag_is_logical(TokenTag t)   { return tag_has(t, TM::LogicalOp); }
inline bool     tag_is_unary(TokenTag t)     { return tag_has(t, TM::UnaryOp); }
inline bool     tag_is_assign(TokenTag t)    { return tag_has(t, TM::AssignOp); }
inline bool     tag_is_ident_like(TokenTag t){ return tag_has(t, TM::IdentLike); }
inline bool     tag_is_keyword(TokenTag t)   { return tag_has(t, TM::Keyword); }
inline bool     tag_is_reserved(TokenTag t)  { return tag_has(t, TM::Reserved); }

// ─── Precedence constants ────────────────────────────────────────────────────

namespace Prec {
constexpr uint8_t Lowest  = 0;
constexpr uint8_t Comma   = 1;
constexpr uint8_t Assign  = 2;
constexpr uint8_t Cond    = 3;
constexpr uint8_t LogOr   = 3;
constexpr uint8_t LogAnd  = 4;
constexpr uint8_t BitOr   = 5;
constexpr uint8_t BitXor  = 6;
constexpr uint8_t BitAnd  = 7;
constexpr uint8_t Eq      = 9;
constexpr uint8_t Rel     = 10;
constexpr uint8_t Shift   = 11;
constexpr uint8_t Add     = 12;
constexpr uint8_t Mul     = 13;
constexpr uint8_t Exp     = 14;
constexpr uint8_t Unary   = 15;
constexpr uint8_t Postfix = 15;
constexpr uint8_t New     = 16;
constexpr uint8_t Call    = 17;
}

// ─── Token flags ─────────────────────────────────────────────────────────────

namespace TF {
constexpr uint8_t NewlineBefore = 1u << 0;
constexpr uint8_t HasEscape     = 1u << 1;
constexpr uint8_t InvalidEscape = 1u << 2;
}

// ─── Token struct ────────────────────────────────────────────────────────────

struct Token {
    uint32_t  start = 0;
    uint32_t  end   = 0;
    TokenTag  tag   = tok_eof;
    uint8_t   flags = 0;

    bool has_newline_before() const { return flags & TF::NewlineBefore; }
    bool has_escape()         const { return flags & TF::HasEscape; }
    bool has_invalid_escape() const { return flags & TF::InvalidEscape; }
};

} // namespace qjsp
