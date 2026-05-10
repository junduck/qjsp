#pragma once

#include "qjsp/atom.hpp"
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace qjsp {

/// Token type — clean enum replacing QuickJS's negative-number convention.
///
/// Single-char tokens (punctuation, operators) use their ASCII value
/// (e.g. Plus = '+', Star = '*'). Values 0–127 are single-char.
/// Multi-char tokens and keywords use values ≥ 256.
enum class TokenKind : uint16_t {
  // ── Single-char (ASCII range 0–127) ────────────────────────────────────
  // Named for parser switch-case readability. Also used for raw char match.
  LParen    = '(',
  RParen    = ')',
  LBrace    = '{',
  RBrace    = '}',
  LBracket  = '[',
  RBracket  = ']',
  Dot       = '.',
  Comma     = ',',
  Semicolon = ';',
  Colon     = ':',
  Plus      = '+',
  Minus     = '-',
  Star      = '*',
  Slash     = '/',
  Percent   = '%',
  Amp       = '&',
  Pipe      = '|',
  Caret     = '^',
  Tilde     = '~',
  Less      = '<',
  Greater   = '>',
  EqAssign  = '=',
  Bang      = '!',
  Question  = '?',
  Hash      = '#',
  SlashChar = '/',

  // ── Sentinels ───────────────────────────────────────────────────────────
  Eof = 0,

  // ── Value tokens (256+) ─────────────────────────────────────────────────
  Identifier = 256,
  Number,
  StringLit,
  RegexpLit,
  TemplateLit,
  PrivateName,

  // ── Comparison / relational ─────────────────────────────────────────────
  Shl,
  Sar,
  Shr,
  Lt,
  Lte,
  Gt,
  Gte,
  Eq,
  StrictEq,
  Neq,
  StrictNeq,

  // ── Logical / arithmetic ────────────────────────────────────────────────
  Land,
  Lor,
  Pow,
  Inc,
  Dec,

  // ── Misc ────────────────────────────────────────────────────────────────
  Arrow,
  Ellipsis,
  DoubleQuestionMark,
  QuestionMarkDot,

  // ── Assignment operators ────────────────────────────────────────────────
  MulAssign,
  DivAssign,
  ModAssign,
  PlusAssign,
  MinusAssign,
  ShlAssign,
  SarAssign,
  ShrAssign,
  AndAssign,
  XorAssign,
  OrAssign,
  PowAssign,
  LandAssign,
  LorAssign,
  DoubleQuestionMarkAssign,

  // ── Keywords ────────────────────────────────────────────────────────────
  KwNull,
  KwFalse,
  KwTrue,
  KwIf,
  KwElse,
  KwReturn,
  KwVar,
  KwThis,
  KwDelete,
  KwVoid,
  KwTypeof,
  KwNew,
  KwIn,
  KwInstanceof,
  KwDo,
  KwWhile,
  KwFor,
  KwBreak,
  KwContinue,
  KwSwitch,
  KwCase,
  KwDefault,
  KwThrow,
  KwTry,
  KwCatch,
  KwFinally,
  KwFunction,
  KwDebugger,
  KwWith,
  KwClass,
  KwConst,
  KwEnum,
  KwExport,
  KwExtends,
  KwImport,
  KwSuper,
  KwImplements,
  KwInterface,
  KwLet,
  KwPackage,
  KwPrivate,
  KwProtected,
  KwPublic,
  KwStatic,
  KwYield,
  KwAwait,
  KwOf,
};

/// True if `k` is any keyword token.
inline constexpr bool isKeyword(TokenKind k) { return k >= TokenKind::KwNull && k <= TokenKind::KwAwait; }

/// True if `k` is an identifier or a keyword (i.e. a word token).
inline constexpr bool isIdentifier(TokenKind k) { return k == TokenKind::Identifier || isKeyword(k); }

/// Keyword lookup table — maps source text to TokenKind.
inline const std::unordered_map<std::string_view, TokenKind> kKeywordTable = {
    {"null", TokenKind::KwNull},
    {"false", TokenKind::KwFalse},
    {"true", TokenKind::KwTrue},
    {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse},
    {"return", TokenKind::KwReturn},
    {"var", TokenKind::KwVar},
    {"this", TokenKind::KwThis},
    {"delete", TokenKind::KwDelete},
    {"void", TokenKind::KwVoid},
    {"typeof", TokenKind::KwTypeof},
    {"new", TokenKind::KwNew},
    {"in", TokenKind::KwIn},
    {"instanceof", TokenKind::KwInstanceof},
    {"do", TokenKind::KwDo},
    {"while", TokenKind::KwWhile},
    {"for", TokenKind::KwFor},
    {"break", TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue},
    {"switch", TokenKind::KwSwitch},
    {"case", TokenKind::KwCase},
    {"default", TokenKind::KwDefault},
    {"throw", TokenKind::KwThrow},
    {"try", TokenKind::KwTry},
    {"catch", TokenKind::KwCatch},
    {"finally", TokenKind::KwFinally},
    {"function", TokenKind::KwFunction},
    {"debugger", TokenKind::KwDebugger},
    {"with", TokenKind::KwWith},
    {"class", TokenKind::KwClass},
    {"const", TokenKind::KwConst},
    {"enum", TokenKind::KwEnum},
    {"export", TokenKind::KwExport},
    {"extends", TokenKind::KwExtends},
    {"import", TokenKind::KwImport},
    {"super", TokenKind::KwSuper},
    {"implements", TokenKind::KwImplements},
    {"interface", TokenKind::KwInterface},
    {"let", TokenKind::KwLet},
    {"package", TokenKind::KwPackage},
    {"private", TokenKind::KwPrivate},
    {"protected", TokenKind::KwProtected},
    {"public", TokenKind::KwPublic},
    {"static", TokenKind::KwStatic},
    {"yield", TokenKind::KwYield},
    {"await", TokenKind::KwAwait},
    {"of", TokenKind::KwOf},
};

} // namespace qjsp
