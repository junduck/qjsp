#pragma once

#include "qjsp/token_kind.hpp"
#include <array>
#include <string_view>

namespace qjsp {

inline constexpr auto TokenName = [] {
  std::array<std::string_view, static_cast<size_t>(TokenKind::KwSize)> arr{};
  // ── Sentinels ───────────────────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::Eof)] = "Eof";
  // ── Single-char (ASCII range 0–127) ────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::LParen)]    = "LParen";
  arr[static_cast<size_t>(TokenKind::RParen)]    = "RParen";
  arr[static_cast<size_t>(TokenKind::LBrace)]    = "LBrace";
  arr[static_cast<size_t>(TokenKind::RBrace)]    = "RBrace";
  arr[static_cast<size_t>(TokenKind::LBracket)]  = "LBracket";
  arr[static_cast<size_t>(TokenKind::RBracket)]  = "RBracket";
  arr[static_cast<size_t>(TokenKind::Dot)]       = "Dot";
  arr[static_cast<size_t>(TokenKind::Comma)]     = "Comma";
  arr[static_cast<size_t>(TokenKind::Semicolon)] = "Semicolon";
  arr[static_cast<size_t>(TokenKind::Colon)]     = "Colon";
  arr[static_cast<size_t>(TokenKind::Plus)]      = "Plus";
  arr[static_cast<size_t>(TokenKind::Minus)]     = "Minus";
  arr[static_cast<size_t>(TokenKind::Star)]      = "Star";
  arr[static_cast<size_t>(TokenKind::Slash)]     = "Slash";
  arr[static_cast<size_t>(TokenKind::Percent)]   = "Percent";
  arr[static_cast<size_t>(TokenKind::Amp)]       = "Amp";
  arr[static_cast<size_t>(TokenKind::Pipe)]      = "Pipe";
  arr[static_cast<size_t>(TokenKind::Caret)]     = "Caret";
  arr[static_cast<size_t>(TokenKind::Tilde)]     = "Tilde";
  arr[static_cast<size_t>(TokenKind::Less)]      = "Less";
  arr[static_cast<size_t>(TokenKind::Greater)]   = "Greater";
  arr[static_cast<size_t>(TokenKind::EqAssign)]  = "EqAssign";
  arr[static_cast<size_t>(TokenKind::Bang)]      = "Bang";
  arr[static_cast<size_t>(TokenKind::Question)]  = "Question";
  arr[static_cast<size_t>(TokenKind::Hash)]      = "Hash";
  arr[static_cast<size_t>(TokenKind::SlashChar)] = "SlashChar";
  // ── Value tokens (256+) ─────────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::Identifier)]  = "Identifier";
  arr[static_cast<size_t>(TokenKind::Number)]      = "Number";
  arr[static_cast<size_t>(TokenKind::StringLit)]   = "StringLit";
  arr[static_cast<size_t>(TokenKind::RegexpLit)]   = "RegexpLit";
  arr[static_cast<size_t>(TokenKind::TemplateLit)] = "TemplateLit";
  arr[static_cast<size_t>(TokenKind::PrivateName)] = "PrivateName";
  // ── Comparison / relational ─────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::Shl)]       = "Shl";
  arr[static_cast<size_t>(TokenKind::Sar)]       = "Sar";
  arr[static_cast<size_t>(TokenKind::Shr)]       = "Shr";
  arr[static_cast<size_t>(TokenKind::Lt)]        = "Lt";
  arr[static_cast<size_t>(TokenKind::Lte)]       = "Lte";
  arr[static_cast<size_t>(TokenKind::Gt)]        = "Gt";
  arr[static_cast<size_t>(TokenKind::Gte)]       = "Gte";
  arr[static_cast<size_t>(TokenKind::Eq)]        = "Eq";
  arr[static_cast<size_t>(TokenKind::StrictEq)]  = "StrictEq";
  arr[static_cast<size_t>(TokenKind::Neq)]       = "Neq";
  arr[static_cast<size_t>(TokenKind::StrictNeq)] = "StrictNeq";
  // ── Logical / arithmetic ────────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::Land)] = "Land";
  arr[static_cast<size_t>(TokenKind::Lor)]  = "Lor";
  arr[static_cast<size_t>(TokenKind::Pow)]  = "Pow";
  arr[static_cast<size_t>(TokenKind::Inc)]  = "Inc";
  arr[static_cast<size_t>(TokenKind::Dec)]  = "Dec";
  // ── Misc ────────────────────────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::Arrow)]              = "Arrow";
  arr[static_cast<size_t>(TokenKind::Ellipsis)]           = "Ellipsis";
  arr[static_cast<size_t>(TokenKind::DoubleQuestionMark)] = "DoubleQuestionMark";
  arr[static_cast<size_t>(TokenKind::QuestionMarkDot)]    = "QuestionMarkDot";
  // ── Assignment operators ────────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::MulAssign)]                = "MulAssign";
  arr[static_cast<size_t>(TokenKind::DivAssign)]                = "DivAssign";
  arr[static_cast<size_t>(TokenKind::ModAssign)]                = "ModAssign";
  arr[static_cast<size_t>(TokenKind::PlusAssign)]               = "PlusAssign";
  arr[static_cast<size_t>(TokenKind::MinusAssign)]              = "MinusAssign";
  arr[static_cast<size_t>(TokenKind::ShlAssign)]                = "ShlAssign";
  arr[static_cast<size_t>(TokenKind::SarAssign)]                = "SarAssign";
  arr[static_cast<size_t>(TokenKind::ShrAssign)]                = "ShrAssign";
  arr[static_cast<size_t>(TokenKind::AndAssign)]                = "AndAssign";
  arr[static_cast<size_t>(TokenKind::XorAssign)]                = "XorAssign";
  arr[static_cast<size_t>(TokenKind::OrAssign)]                 = "OrAssign";
  arr[static_cast<size_t>(TokenKind::PowAssign)]                = "PowAssign";
  arr[static_cast<size_t>(TokenKind::LandAssign)]               = "LandAssign";
  arr[static_cast<size_t>(TokenKind::LorAssign)]                = "LorAssign";
  arr[static_cast<size_t>(TokenKind::DoubleQuestionMarkAssign)] = "DoubleQuestionMarkAssign";
  // ── Keywords ────────────────────────────────────────────────────────────
  arr[static_cast<size_t>(TokenKind::KwNull)]       = "KwNull";
  arr[static_cast<size_t>(TokenKind::KwFalse)]      = "KwFalse";
  arr[static_cast<size_t>(TokenKind::KwTrue)]       = "KwTrue";
  arr[static_cast<size_t>(TokenKind::KwIf)]         = "KwIf";
  arr[static_cast<size_t>(TokenKind::KwElse)]       = "KwElse";
  arr[static_cast<size_t>(TokenKind::KwReturn)]     = "KwReturn";
  arr[static_cast<size_t>(TokenKind::KwVar)]        = "KwVar";
  arr[static_cast<size_t>(TokenKind::KwThis)]       = "KwThis";
  arr[static_cast<size_t>(TokenKind::KwDelete)]     = "KwDelete";
  arr[static_cast<size_t>(TokenKind::KwVoid)]       = "KwVoid";
  arr[static_cast<size_t>(TokenKind::KwTypeof)]     = "KwTypeof";
  arr[static_cast<size_t>(TokenKind::KwNew)]        = "KwNew";
  arr[static_cast<size_t>(TokenKind::KwIn)]         = "KwIn";
  arr[static_cast<size_t>(TokenKind::KwInstanceof)] = "KwInstanceof";
  arr[static_cast<size_t>(TokenKind::KwDo)]         = "KwDo";
  arr[static_cast<size_t>(TokenKind::KwWhile)]      = "KwWhile";
  arr[static_cast<size_t>(TokenKind::KwFor)]        = "KwFor";
  arr[static_cast<size_t>(TokenKind::KwBreak)]      = "KwBreak";
  arr[static_cast<size_t>(TokenKind::KwContinue)]   = "KwContinue";
  arr[static_cast<size_t>(TokenKind::KwSwitch)]     = "KwSwitch";
  arr[static_cast<size_t>(TokenKind::KwCase)]       = "KwCase";
  arr[static_cast<size_t>(TokenKind::KwDefault)]    = "KwDefault";
  arr[static_cast<size_t>(TokenKind::KwThrow)]      = "KwThrow";
  arr[static_cast<size_t>(TokenKind::KwTry)]        = "KwTry";
  arr[static_cast<size_t>(TokenKind::KwCatch)]      = "KwCatch";
  arr[static_cast<size_t>(TokenKind::KwFinally)]    = "KwFinally";
  arr[static_cast<size_t>(TokenKind::KwFunction)]   = "KwFunction";
  arr[static_cast<size_t>(TokenKind::KwDebugger)]   = "KwDebugger";
  arr[static_cast<size_t>(TokenKind::KwWith)]       = "KwWith";
  arr[static_cast<size_t>(TokenKind::KwClass)]      = "KwClass";
  arr[static_cast<size_t>(TokenKind::KwConst)]      = "KwConst";
  arr[static_cast<size_t>(TokenKind::KwEnum)]       = "KwEnum";
  arr[static_cast<size_t>(TokenKind::KwExport)]     = "KwExport";
  arr[static_cast<size_t>(TokenKind::KwExtends)]    = "KwExtends";
  arr[static_cast<size_t>(TokenKind::KwImport)]     = "KwImport";
  arr[static_cast<size_t>(TokenKind::KwSuper)]      = "KwSuper";
  arr[static_cast<size_t>(TokenKind::KwImplements)] = "KwImplements";
  arr[static_cast<size_t>(TokenKind::KwInterface)]  = "KwInterface";
  arr[static_cast<size_t>(TokenKind::KwLet)]        = "KwLet";
  arr[static_cast<size_t>(TokenKind::KwPackage)]    = "KwPackage";
  arr[static_cast<size_t>(TokenKind::KwPrivate)]    = "KwPrivate";
  arr[static_cast<size_t>(TokenKind::KwProtected)]  = "KwProtected";
  arr[static_cast<size_t>(TokenKind::KwPublic)]     = "KwPublic";
  arr[static_cast<size_t>(TokenKind::KwStatic)]     = "KwStatic";
  arr[static_cast<size_t>(TokenKind::KwYield)]      = "KwYield";
  arr[static_cast<size_t>(TokenKind::KwAwait)]      = "KwAwait";
  arr[static_cast<size_t>(TokenKind::KwOf)]         = "KwOf";
  return arr;
}();

} // namespace qjsp
