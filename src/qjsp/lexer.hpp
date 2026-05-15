#pragma once

#include "atom.hpp"
#include "token_kind.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace qjsp {

struct Engine;

// ─── Lexer error types ──────────────────────────────────────────────────────

enum class LexErrorKind : uint8_t {
  None,

  UnterminatedString,
  UnterminatedTemplate,
  UnterminatedComment,
  UnterminatedRegex,

  InvalidEscape,
  InvalidHexEscape,
  InvalidUnicodeEscape,
  InvalidOctalEscape,
  InvalidUtf8,

  InvalidNumber,
  InvalidPrivateName,
  UnexpectedChar,
};

struct SourceLoc {
  uint32_t line   = 1;
  uint32_t column = 1;
};

struct LexError {
  LexErrorKind kind   = LexErrorKind::None;
  SourceLoc    loc;

  explicit operator bool() const { return kind != LexErrorKind::None; }
};

const char *lex_error_message(LexErrorKind k);

struct Token {
  TokenKind kind     = TokenKind::Eof;
  const uint8_t *ptr = nullptr;

  // string / template literal
  std::string str_val;
  uint32_t str_len = 0;
  int str_sep      = 0;

  // numeric
  double num_val = 0;

  // identifier (also populated for keywords)
  Atom ident_atom        = kAtomNull;
  bool ident_has_escape  = false;
  bool ident_is_reserved = false;

  // regexp — verbatim slices of the source buffer
  std::string_view regexp_body;
  std::string_view regexp_flags;
};

// ─── Lexer ──────────────────────────────────────────────────────────────────

struct Lexer {
  Engine *e_               = nullptr;
  const char *filename     = nullptr;
  const uint8_t *buf_start = nullptr;
  const uint8_t *buf_ptr   = nullptr;
  const uint8_t *buf_end   = nullptr;
  const uint8_t *last_ptr  = nullptr;
  const uint8_t *line_start_ = nullptr;
  uint32_t line_            = 1;
  Token token;
  LexError error_;
  bool got_lf              = false;
  bool allow_html_comments = false;

  void init(Engine *e, const char *filename, const uint8_t *source, size_t source_len);
  void reset(const uint8_t *source, size_t source_len);
  size_t buf_pos() const { return static_cast<size_t>(buf_ptr - buf_start); }
  SourceLoc source_loc(const uint8_t *p) const {
    return {line_, static_cast<uint32_t>(p - line_start_ + 1)};
  }

  bool next_token();
  TokenKind peek_token(bool no_line_terminator);
  bool parse_regexp();
  bool reparse_as_regexp();

  static void skip_shebang(const uint8_t **pp, const uint8_t *buf_end);

private:
  bool fail(LexErrorKind kind, const uint8_t *ptr) {
    error_.kind = kind;
    error_.loc  = source_loc(ptr);
    return false;
  }

  void advance_line(const uint8_t *next_line_start) {
    line_++;
    line_start_ = next_line_start;
  }

  bool parse_ident_token(int first_c, bool has_escape);
  bool parse_private_name();
  bool parse_template_part(const uint8_t *p);
  bool parse_string(int sep, bool do_throw, const uint8_t *p, Token *out, const uint8_t **pp);
  bool parse_number(const uint8_t *p);

  static int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp);
  static void append_utf8(std::string &buf, unsigned int c);
  static bool lre_is_space(uint32_t c);
  int parse_escape(const uint8_t **pp, bool allow_utf16);
  static bool is_digit(int c) { return c >= '0' && c <= '9'; }
  static int from_hex(int c);
  static bool is_surrogate(uint32_t c);
  static bool is_hi_surrogate(uint32_t c);
  static bool is_lo_surrogate(uint32_t c);
  static uint32_t from_surrogate(uint32_t hi, uint32_t lo);
};

// ─── inline constants ───────────────────────────────────────────────────────

constexpr int CP_NBSP           = 0x00A0;
constexpr int CP_BOM            = 0xFEFF;
constexpr int CP_LS             = 0x2028;
constexpr int CP_PS             = 0x2029;
constexpr int UTF8_CHAR_LEN_MAX = 6;

} // namespace qjsp
