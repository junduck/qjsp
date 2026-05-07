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

/// Convert an Atom value to its keyword token type.
inline constexpr int tok_from_atom(Atom atom) { return TOK_FIRST_KEYWORD + static_cast<int>(atom) - 1; }

/// Convert a keyword token type back to the Atom.
inline constexpr Atom atom_from_keyword_tok(int tok) { return static_cast<Atom>(tok - TOK_FIRST_KEYWORD + 1); }

// Individual keyword token constants
constexpr int TOK_NULL       = tok_from_atom(static_cast<Atom>(AtomEnum::_null));
constexpr int TOK_FALSE      = tok_from_atom(static_cast<Atom>(AtomEnum::_false));
constexpr int TOK_TRUE       = tok_from_atom(static_cast<Atom>(AtomEnum::_true));
constexpr int TOK_IF         = tok_from_atom(static_cast<Atom>(AtomEnum::_if));
constexpr int TOK_ELSE       = tok_from_atom(static_cast<Atom>(AtomEnum::_else));
constexpr int TOK_RETURN     = tok_from_atom(static_cast<Atom>(AtomEnum::_return));
constexpr int TOK_VAR        = tok_from_atom(static_cast<Atom>(AtomEnum::_var));
constexpr int TOK_THIS       = tok_from_atom(static_cast<Atom>(AtomEnum::_this));
constexpr int TOK_DELETE     = tok_from_atom(static_cast<Atom>(AtomEnum::_delete));
constexpr int TOK_VOID       = tok_from_atom(static_cast<Atom>(AtomEnum::_void));
constexpr int TOK_TYPEOF     = tok_from_atom(static_cast<Atom>(AtomEnum::_typeof));
constexpr int TOK_NEW        = tok_from_atom(static_cast<Atom>(AtomEnum::_new));
constexpr int TOK_IN         = tok_from_atom(static_cast<Atom>(AtomEnum::_in));
constexpr int TOK_INSTANCEOF = tok_from_atom(static_cast<Atom>(AtomEnum::_instanceof));
constexpr int TOK_DO         = tok_from_atom(static_cast<Atom>(AtomEnum::_do));
constexpr int TOK_WHILE      = tok_from_atom(static_cast<Atom>(AtomEnum::_while));
constexpr int TOK_FOR        = tok_from_atom(static_cast<Atom>(AtomEnum::_for));
constexpr int TOK_BREAK      = tok_from_atom(static_cast<Atom>(AtomEnum::_break));
constexpr int TOK_CONTINUE   = tok_from_atom(static_cast<Atom>(AtomEnum::_continue));
constexpr int TOK_SWITCH     = tok_from_atom(static_cast<Atom>(AtomEnum::_switch));
constexpr int TOK_CASE       = tok_from_atom(static_cast<Atom>(AtomEnum::_case));
constexpr int TOK_DEFAULT    = tok_from_atom(static_cast<Atom>(AtomEnum::_default));
constexpr int TOK_THROW      = tok_from_atom(static_cast<Atom>(AtomEnum::_throw));
constexpr int TOK_TRY        = tok_from_atom(static_cast<Atom>(AtomEnum::_try));
constexpr int TOK_CATCH      = tok_from_atom(static_cast<Atom>(AtomEnum::_catch));
constexpr int TOK_FINALLY    = tok_from_atom(static_cast<Atom>(AtomEnum::_finally));
constexpr int TOK_FUNCTION   = tok_from_atom(static_cast<Atom>(AtomEnum::_function));
constexpr int TOK_DEBUGGER   = tok_from_atom(static_cast<Atom>(AtomEnum::_debugger));
constexpr int TOK_WITH       = tok_from_atom(static_cast<Atom>(AtomEnum::_with));
constexpr int TOK_CLASS      = tok_from_atom(static_cast<Atom>(AtomEnum::_class));
constexpr int TOK_CONST      = tok_from_atom(static_cast<Atom>(AtomEnum::_const));
constexpr int TOK_ENUM       = tok_from_atom(static_cast<Atom>(AtomEnum::_enum));
constexpr int TOK_EXPORT     = tok_from_atom(static_cast<Atom>(AtomEnum::_export));
constexpr int TOK_EXTENDS    = tok_from_atom(static_cast<Atom>(AtomEnum::_extends));
constexpr int TOK_IMPORT     = tok_from_atom(static_cast<Atom>(AtomEnum::_import));
constexpr int TOK_SUPER      = tok_from_atom(static_cast<Atom>(AtomEnum::_super));
constexpr int TOK_IMPLEMENTS = tok_from_atom(static_cast<Atom>(AtomEnum::_implements));
constexpr int TOK_INTERFACE  = tok_from_atom(static_cast<Atom>(AtomEnum::_interface));
constexpr int TOK_LET        = tok_from_atom(static_cast<Atom>(AtomEnum::_let));
constexpr int TOK_PACKAGE    = tok_from_atom(static_cast<Atom>(AtomEnum::_package));
constexpr int TOK_PRIVATE    = tok_from_atom(static_cast<Atom>(AtomEnum::_private));
constexpr int TOK_PROTECTED  = tok_from_atom(static_cast<Atom>(AtomEnum::_protected));
constexpr int TOK_PUBLIC     = tok_from_atom(static_cast<Atom>(AtomEnum::_public));
constexpr int TOK_STATIC     = tok_from_atom(static_cast<Atom>(AtomEnum::_static));
constexpr int TOK_YIELD      = tok_from_atom(static_cast<Atom>(AtomEnum::_yield));
constexpr int TOK_AWAIT      = tok_from_atom(static_cast<Atom>(AtomEnum::_await));
constexpr int TOK_OF         = tok_from_atom(static_cast<Atom>(AtomEnum::of));

/// True if `tok` is any keyword token.
inline constexpr bool is_keyword(int tok) { return tok >= TOK_FIRST_KEYWORD && tok <= TOK_LAST_KEYWORD; }

/// True if `tok` is an identifier or keyword (i.e. a word token).
inline constexpr bool token_is_ident(int tok) { return tok == TOK_IDENT || is_keyword(tok); }

// ─── Token ──────────────────────────────────────────────────────────────────

struct Token {
  int type           = TOK_EOF;
  const uint8_t *ptr = nullptr; // position in source buffer

  union {
    struct {
      char *str;    // heap-allocated, null-terminated
      uint32_t len; // byte length
      int sep;      // opening quote char, or '`' / '}' for templates
    } str;
    struct {
      double val;
    } num;
    struct {
      Atom atom;
      bool has_escape;
      bool is_reserved;
    } ident;
    struct {
      char *body; // heap-allocated, null-terminated
      uint32_t body_len;
      char *flags; // heap-allocated, null-terminated
      uint32_t flags_len;
    } regexp;
  } u{};

  Token() { std::memset(&u, 0, sizeof(u)); }

  /// Free any heap-allocated data owned by this token.
  void free_token();
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
  static void copy_str(char *&dst, uint32_t &dst_len, const std::string &buf);
};

// ─── inline constants ───────────────────────────────────────────────────────

constexpr int CP_NBSP           = 0x00A0;
constexpr int CP_BOM            = 0xFEFF;
constexpr int CP_LS             = 0x2028;
constexpr int CP_PS             = 0x2029;
constexpr int UTF8_CHAR_LEN_MAX = 6;

} // namespace qjsp
