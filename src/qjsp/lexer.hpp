#pragma once

#include "qjsp/atom.hpp"
#include <cstdint>
#include <cstring>
#include <string>

namespace qjsp {

struct Runtime;

// ─── Token types ────────────────────────────────────────────────────────────
//
// Same numbering as original QuickJS so the parser port works directly.
// Negative values: value-tokens and operators.
// Keywords are mapped from atom index.
//
//   TOK_NUMBER = -128 … TOK_EOF = -86
//   TOK_NULL through TOK_AWAIT  (keyword range, derived from atom enum)
//
// Single-char tokens (e.g. '(', '}') use the char value itself.

constexpr int TOK_NUMBER                      = -128;
constexpr int TOK_STRING                      = -127;
constexpr int TOK_TEMPLATE                    = -126;
constexpr int TOK_IDENT                       = -125;
constexpr int TOK_REGEXP                      = -124;
constexpr int TOK_MUL_ASSIGN                  = -123;
constexpr int TOK_DIV_ASSIGN                  = -122;
constexpr int TOK_MOD_ASSIGN                  = -121;
constexpr int TOK_PLUS_ASSIGN                 = -120;
constexpr int TOK_MINUS_ASSIGN                = -119;
constexpr int TOK_SHL_ASSIGN                  = -118;
constexpr int TOK_SAR_ASSIGN                  = -117;
constexpr int TOK_SHR_ASSIGN                  = -116;
constexpr int TOK_AND_ASSIGN                  = -115;
constexpr int TOK_XOR_ASSIGN                  = -114;
constexpr int TOK_OR_ASSIGN                   = -113;
constexpr int TOK_POW_ASSIGN                  = -112;
constexpr int TOK_LAND_ASSIGN                 = -111;
constexpr int TOK_LOR_ASSIGN                  = -110;
constexpr int TOK_DOUBLE_QUESTION_MARK_ASSIGN = -109;
constexpr int TOK_DEC                         = -108;
constexpr int TOK_INC                         = -107;
constexpr int TOK_SHL                         = -106;
constexpr int TOK_SAR                         = -105;
constexpr int TOK_SHR                         = -104;
constexpr int TOK_LT                          = -103;
constexpr int TOK_LTE                         = -102;
constexpr int TOK_GT                          = -101;
constexpr int TOK_GTE                         = -100;
constexpr int TOK_EQ                          = -99;
constexpr int TOK_STRICT_EQ                   = -98;
constexpr int TOK_NEQ                         = -97;
constexpr int TOK_STRICT_NEQ                  = -96;
constexpr int TOK_LAND                        = -95;
constexpr int TOK_LOR                         = -94;
constexpr int TOK_POW                         = -93;
constexpr int TOK_ARROW                       = -92;
constexpr int TOK_ELLIPSIS                    = -91;
constexpr int TOK_DOUBLE_QUESTION_MARK        = -90;
constexpr int TOK_QUESTION_MARK_DOT           = -89;
constexpr int TOK_ERROR                       = -88;
constexpr int TOK_PRIVATE_NAME                = -87;
constexpr int TOK_EOF                         = -86;

// Keyword range
constexpr int TOK_FIRST_KEYWORD = -85;
constexpr int TOK_LAST_KEYWORD  = -40;

// atom_token(Atom) is defined in atom.hpp — resolves keywords to TOK_* values

// Individual keyword token constants
constexpr int TOK_NULL       = atom_token(static_cast<Atom>(AtomEnum::_null));
constexpr int TOK_FALSE      = atom_token(static_cast<Atom>(AtomEnum::_false));
constexpr int TOK_TRUE       = atom_token(static_cast<Atom>(AtomEnum::_true));
constexpr int TOK_IF         = atom_token(static_cast<Atom>(AtomEnum::_if));
constexpr int TOK_ELSE       = atom_token(static_cast<Atom>(AtomEnum::_else));
constexpr int TOK_RETURN     = atom_token(static_cast<Atom>(AtomEnum::_return));
constexpr int TOK_VAR        = atom_token(static_cast<Atom>(AtomEnum::_var));
constexpr int TOK_THIS       = atom_token(static_cast<Atom>(AtomEnum::_this));
constexpr int TOK_DELETE     = atom_token(static_cast<Atom>(AtomEnum::_delete));
constexpr int TOK_VOID       = atom_token(static_cast<Atom>(AtomEnum::_void));
constexpr int TOK_TYPEOF     = atom_token(static_cast<Atom>(AtomEnum::_typeof));
constexpr int TOK_NEW        = atom_token(static_cast<Atom>(AtomEnum::_new));
constexpr int TOK_IN         = atom_token(static_cast<Atom>(AtomEnum::_in));
constexpr int TOK_INSTANCEOF = atom_token(static_cast<Atom>(AtomEnum::_instanceof));
constexpr int TOK_DO         = atom_token(static_cast<Atom>(AtomEnum::_do));
constexpr int TOK_WHILE      = atom_token(static_cast<Atom>(AtomEnum::_while));
constexpr int TOK_FOR        = atom_token(static_cast<Atom>(AtomEnum::_for));
constexpr int TOK_BREAK      = atom_token(static_cast<Atom>(AtomEnum::_break));
constexpr int TOK_CONTINUE   = atom_token(static_cast<Atom>(AtomEnum::_continue));
constexpr int TOK_SWITCH     = atom_token(static_cast<Atom>(AtomEnum::_switch));
constexpr int TOK_CASE       = atom_token(static_cast<Atom>(AtomEnum::_case));
constexpr int TOK_DEFAULT    = atom_token(static_cast<Atom>(AtomEnum::_default));
constexpr int TOK_THROW      = atom_token(static_cast<Atom>(AtomEnum::_throw));
constexpr int TOK_TRY        = atom_token(static_cast<Atom>(AtomEnum::_try));
constexpr int TOK_CATCH      = atom_token(static_cast<Atom>(AtomEnum::_catch));
constexpr int TOK_FINALLY    = atom_token(static_cast<Atom>(AtomEnum::_finally));
constexpr int TOK_FUNCTION   = atom_token(static_cast<Atom>(AtomEnum::_function));
constexpr int TOK_DEBUGGER   = atom_token(static_cast<Atom>(AtomEnum::_debugger));
constexpr int TOK_WITH       = atom_token(static_cast<Atom>(AtomEnum::_with));
constexpr int TOK_CLASS      = atom_token(static_cast<Atom>(AtomEnum::_class));
constexpr int TOK_CONST      = atom_token(static_cast<Atom>(AtomEnum::_const));
constexpr int TOK_ENUM       = atom_token(static_cast<Atom>(AtomEnum::_enum));
constexpr int TOK_EXPORT     = atom_token(static_cast<Atom>(AtomEnum::_export));
constexpr int TOK_EXTENDS    = atom_token(static_cast<Atom>(AtomEnum::_extends));
constexpr int TOK_IMPORT     = atom_token(static_cast<Atom>(AtomEnum::_import));
constexpr int TOK_SUPER      = atom_token(static_cast<Atom>(AtomEnum::_super));
constexpr int TOK_IMPLEMENTS = atom_token(static_cast<Atom>(AtomEnum::_implements));
constexpr int TOK_INTERFACE  = atom_token(static_cast<Atom>(AtomEnum::_interface));
constexpr int TOK_LET        = atom_token(static_cast<Atom>(AtomEnum::_let));
constexpr int TOK_PACKAGE    = atom_token(static_cast<Atom>(AtomEnum::_package));
constexpr int TOK_PRIVATE    = atom_token(static_cast<Atom>(AtomEnum::_private));
constexpr int TOK_PROTECTED  = atom_token(static_cast<Atom>(AtomEnum::_protected));
constexpr int TOK_PUBLIC     = atom_token(static_cast<Atom>(AtomEnum::_public));
constexpr int TOK_STATIC     = atom_token(static_cast<Atom>(AtomEnum::_static));
constexpr int TOK_YIELD      = atom_token(static_cast<Atom>(AtomEnum::_yield));
constexpr int TOK_AWAIT      = atom_token(static_cast<Atom>(AtomEnum::_await));
constexpr int TOK_OF         = atom_token(static_cast<Atom>(AtomEnum::of));

/// True if `tok` is any keyword token.
inline constexpr bool is_keyword(int tok) { return tok >= TOK_FIRST_KEYWORD && tok <= TOK_LAST_KEYWORD; }

/// True if `tok` is an identifier or keyword (i.e. a word token).
inline constexpr bool token_is_ident(int tok) { return tok == TOK_IDENT || is_keyword(tok); }

// ─── Token ──────────────────────────────────────────────────────────────────

struct Token {
  int type           = TOK_EOF;
  const uint8_t *ptr = nullptr; // position in source buffer

  // string / template literal
  std::string str_val;
  uint32_t str_len = 0;
  int str_sep      = 0;

  // numeric
  double num_val = 0;

  // identifier
  Atom ident_atom     = kAtomNull;
  bool ident_has_escape  = false;
  bool ident_is_reserved = false;

  // regexp
  std::string regexp_body;
  std::string regexp_flags;
  uint32_t regexp_body_len  = 0;
  uint32_t regexp_flags_len = 0;
};

inline bool is_keyword_token(int tok) { return is_keyword(tok); }

// ─── Lexer ──────────────────────────────────────────────────────────────────

struct Lexer {
  Runtime *rt              = nullptr;
  const char *filename     = nullptr;
  const uint8_t *buf_start = nullptr;
  const uint8_t *buf_ptr   = nullptr;
  const uint8_t *buf_end   = nullptr;
  const uint8_t *last_ptr  = nullptr;
  Token token;
  bool got_lf              = false;
  bool allow_html_comments = false;

  void init(Runtime *rt, const char *filename, const uint8_t *source, size_t source_len);

  /// Advance to the next token. Returns true on success, false on error.
  bool next_token();

  /// Look ahead at the next token without consuming it.
  int peek_token(bool no_line_terminator);

  /// Skip shebang (#!...) at the start of input.
  static void skip_shebang(const uint8_t **pp, const uint8_t *buf_end);

private:
  bool parse_ident_token(int first_c, bool has_escape);
  bool parse_private_name();
  bool parse_template_part(const uint8_t *p);
  bool parse_string(int sep, bool do_throw, const uint8_t *p, Token *out, const uint8_t **pp);
  bool parse_regexp();
  bool parse_number(const uint8_t *p);
  void update_token_ident();

  // character-level helpers (static)
  static int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp);
  static void append_utf8(std::string &buf, unsigned int c);
  static bool lre_is_space(uint32_t c);
  static bool lre_js_is_ident_first(uint32_t c);
  static bool lre_js_is_ident_next(uint32_t c);
  static int parse_escape(const uint8_t **pp, bool allow_utf16);
  static bool is_digit(int c) { return c >= '0' && c <= '9'; }
  static int from_hex(int c);
  static bool is_surrogate(uint32_t c);
  static bool is_hi_surrogate(uint32_t c);
  static bool is_lo_surrogate(uint32_t c);
  static uint32_t from_surrogate(uint32_t hi, uint32_t lo);

  /// Allocate a null-terminated copy of `buf`, store in `*dst` / `*dst_len`.
  static void copy_str(std::string &dst, uint32_t &dst_len, const std::string &buf);
};

// ─── inline constants ───────────────────────────────────────────────────────

constexpr int CP_NBSP           = 0x00A0;
constexpr int CP_BOM            = 0xFEFF;
constexpr int CP_LS             = 0x2028;
constexpr int CP_PS             = 0x2029;
constexpr int UTF8_CHAR_LEN_MAX = 6;

} // namespace qjsp
