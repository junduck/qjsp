#include "qjsp/lexer.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/string.hpp"
#include "qjsp/unicode_id.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>

namespace qjsp {

const char *lex_error_message(LexErrorKind k) {
  switch (k) {
  case LexErrorKind::None:
    return "no error";
  case LexErrorKind::UnterminatedString:
    return "unterminated string literal";
  case LexErrorKind::UnterminatedTemplate:
    return "unterminated template literal";
  case LexErrorKind::UnterminatedComment:
    return "unterminated multi-line comment";
  case LexErrorKind::UnterminatedRegex:
    return "unterminated regular expression literal";
  case LexErrorKind::InvalidEscape:
    return "malformed escape sequence";
  case LexErrorKind::InvalidHexEscape:
    return "invalid hexadecimal escape sequence";
  case LexErrorKind::InvalidUnicodeEscape:
    return "invalid Unicode escape sequence";
  case LexErrorKind::InvalidOctalEscape:
    return "octal escape sequences are not allowed";
  case LexErrorKind::InvalidUtf8:
    return "invalid UTF-8 sequence";
  case LexErrorKind::InvalidNumber:
    return "invalid number literal";
  case LexErrorKind::InvalidPrivateName:
    return "invalid private name";
  case LexErrorKind::UnexpectedChar:
    return "unexpected character";
  }
  return "unknown error";
}

// ─── Character helpers ──────────────────────────────────────────────────────

int Lexer::unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp) {
  int c, minc;
  c = *p;
  if (c < 0x80) {
    *pp = p + 1;
    return c;
  }
  if (c < 0xC0) {
    *pp = p + 1;
    return -1;
  }
  if (c < 0xE0) {
    if (max_len < 2)
      return -1;
    minc = 0x80;
    c    = c & 0x1F;
    c    = (c << 6) | (p[1] & 0x3F);
    *pp  = p + 2;
  } else if (c < 0xF0) {
    if (max_len < 3)
      return -1;
    minc = 0x800;
    c    = c & 0x0F;
    c    = (c << 6) | (p[1] & 0x3F);
    c    = (c << 6) | (p[2] & 0x3F);
    *pp  = p + 3;
  } else if (c < 0xF8) {
    if (max_len < 4)
      return -1;
    minc = 0x10000;
    c    = c & 0x07;
    c    = (c << 6) | (p[1] & 0x3F);
    c    = (c << 6) | (p[2] & 0x3F);
    c    = (c << 6) | (p[3] & 0x3F);
    *pp  = p + 4;
  } else if (c < 0xFC) {
    if (max_len < 5)
      return -1;
    minc = 0x200000;
    c    = c & 0x03;
    c    = (c << 6) | (p[1] & 0x3F);
    c    = (c << 6) | (p[2] & 0x3F);
    c    = (c << 6) | (p[3] & 0x3F);
    c    = (c << 6) | (p[4] & 0x3F);
    *pp  = p + 5;
  } else {
    if (max_len < 6)
      return -1;
    minc = 0x4000000;
    c    = c & 0x01;
    c    = (c << 6) | (p[1] & 0x3F);
    c    = (c << 6) | (p[2] & 0x3F);
    c    = (c << 6) | (p[3] & 0x3F);
    c    = (c << 6) | (p[4] & 0x3F);
    c    = (c << 6) | (p[5] & 0x3F);
    *pp  = p + 6;
  }
  if (c < minc || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
    return -1;
  return c;
}

void Lexer::append_utf8(std::string &buf, unsigned int c) {
  if (c < 0x80) {
    buf.push_back(static_cast<char>(c));
  } else if (c < 0x800) {
    buf.push_back(static_cast<char>((c >> 6) | 0xC0));
    buf.push_back(static_cast<char>((c & 0x3F) | 0x80));
  } else if (c < 0x10000) {
    buf.push_back(static_cast<char>((c >> 12) | 0xE0));
    buf.push_back(static_cast<char>(((c >> 6) & 0x3F) | 0x80));
    buf.push_back(static_cast<char>((c & 0x3F) | 0x80));
  } else {
    buf.push_back(static_cast<char>((c >> 18) | 0xF0));
    buf.push_back(static_cast<char>(((c >> 12) & 0x3F) | 0x80));
    buf.push_back(static_cast<char>(((c >> 6) & 0x3F) | 0x80));
    buf.push_back(static_cast<char>((c & 0x3F) | 0x80));
  }
}

int Lexer::from_hex(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

bool Lexer::is_surrogate(uint32_t c) { return (c >> 11) == (0xD800 >> 11); }
bool Lexer::is_hi_surrogate(uint32_t c) { return (c >> 10) == (0xD800 >> 10); }
bool Lexer::is_lo_surrogate(uint32_t c) { return (c >> 10) == (0xDC00 >> 10); }

uint32_t Lexer::from_surrogate(uint32_t hi, uint32_t lo) { return 0x10000u + 0x400u * (hi - 0xD800u) + (lo - 0xDC00u); }

/* -------------------------------------------------------------------------- */
/*  Unicode classification                                                   */
/* -------------------------------------------------------------------------- */

bool Lexer::lre_is_space(uint32_t c) {
  if (c < 256)
    return c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20 || c == 0xA0 || c == 0xFEFF;
  return (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

/* -------------------------------------------------------------------------- */
/*  Escape sequence parser                                                    */
/* -------------------------------------------------------------------------- */

int Lexer::parse_escape(const uint8_t **pp, bool allow_utf16) {
  const uint8_t *p = *pp;
  if (p >= buf_end)
    return -1;
  uint32_t c = *p++;

  switch (c) {
  case 'b':
    c = '\b';
    break;
  case 'f':
    c = '\f';
    break;
  case 'n':
    c = '\n';
    break;
  case 'r':
    c = '\r';
    break;
  case 't':
    c = '\t';
    break;
  case 'v':
    c = '\v';
    break;
  case 'x': {
    if (p + 1 >= buf_end)
      return -1;
    int h0 = from_hex(*p++), h1 = from_hex(*p++);
    if (h0 < 0 || h1 < 0)
      return -1;
    c = (static_cast<unsigned>(h0) << 4) | static_cast<unsigned>(h1);
    break;
  }
  case 'u': {
    if (*p == '{' && allow_utf16) {
      p++;
      c = 0;
      for (;;) {
        if (p >= buf_end)
          return -1;
        int h = from_hex(*p++);
        if (h < 0)
          return -1;
        c = (c << 4) | static_cast<unsigned>(h);
        if (c > 0x10FFFF)
          return -1;
        if (*p == '}')
          break;
      }
      p++;
    } else {
      if (p + 3 >= buf_end)
        return -1; // need 4 hex chars
      c = 0;
      for (int i = 0; i < 4; i++) {
        int h = from_hex(*p++);
        if (h < 0)
          return -1;
        c = (c << 4) | static_cast<unsigned>(h);
      }
      if (is_hi_surrogate(c) && allow_utf16 && p + 5 < buf_end && p[0] == '\\' && p[1] == 'u') {
        uint32_t c1 = 0;
        for (int i = 0; i < 4; i++) {
          int h = from_hex(p[2 + i]);
          if (h < 0)
            break;
          c1 = (c1 << 4) | static_cast<unsigned>(h);
        }
        if (is_lo_surrogate(c1)) {
          p += 6;
          c = from_surrogate(c, c1);
        }
      }
    }
    break;
  }
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
    c -= '0';
    if (allow_utf16) {
      if (c != 0 || is_digit(*p))
        return -1;
    } else {
      if (p >= buf_end)
        break;
      unsigned v = static_cast<unsigned>(*p - '0');
      if (v > 7)
        break;
      c = (c << 3) | v;
      p++;
      if (c >= 32)
        break;
      if (p >= buf_end)
        break;
      v = static_cast<unsigned>(*p - '0');
      if (v > 7)
        break;
      c = (c << 3) | v;
      p++;
    }
    break;
  default:
    return -2;
  }
  *pp = p;
  return static_cast<int>(c);
}

// ─── Lexer initialisation ───────────────────────────────────────────────────

void Lexer::init(Engine *e_val, const char *filename_val, const uint8_t *source, size_t source_len) {
  e_        = e_val;
  filename  = filename_val;
  buf_start = source;
  buf_ptr   = source;
  buf_end   = source + source_len;
  last_ptr  = source;
  got_lf    = false;
  error_    = LexError{};
}

void Lexer::reset(const uint8_t *source, size_t source_len) {
  buf_start = source;
  buf_ptr   = source;
  buf_end   = source + source_len;
  last_ptr  = source;
  got_lf    = false;
  token     = Token{};
  error_    = LexError{};
}

// ─── next_token() — the main dispatcher ─────────────────────────────────────

bool Lexer::next_token() {
  const uint8_t *p;
  int c;
  bool ident_has_escape;

  p = last_ptr = buf_ptr;
  got_lf       = false;

redo:
  token.ptr = p;
  c         = *p;

  switch (c) {
  case 0:
    if (p >= buf_end)
      token.kind = TokenKind::Eof;
    else
      goto def_token;
    break;

  case '`':
    if (!parse_template_part(p + 1))
      return false;
    p = buf_ptr;
    break;

  case '\'':
  case '\"':
    if (!parse_string(c, true, p + 1, &token, &p))
      return false;
    break;

  case '\r':
    if (p[1] == '\n')
      p++;
    // fall through
  case '\n':
    p++;
  line_terminator:
    got_lf = true;
    goto redo;

  case '\f':
  case '\v':
  case ' ':
  case '\t':
    p++;
    goto redo;

  case '/':
    if (p[1] == '*') {
      p += 2;
      for (;;) {
        if (*p == '\0' && p >= buf_end)
          return fail(LexErrorKind::UnterminatedComment, p);
        if (p[0] == '*' && p[1] == '/') {
          p += 2;
          break;
        }
        if (*p == '\n' || *p == '\r') {
          got_lf = true;
          p++;
        } else if (*p >= 0x80) {
          const uint8_t *p_next;
          c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next);
          if (c == CP_LS || c == CP_PS)
            got_lf = true;
          else if (c == -1)
            p++;
          p = p_next;
        } else
          p++;
      }
      goto redo;
    } else if (p[1] == '/') {
      p += 2;
    skip_line_comment:
      for (;;) {
        if (*p == '\0' && p >= buf_end)
          break;
        if (*p == '\r' || *p == '\n')
          break;
        if (*p >= 0x80) {
          const uint8_t *p_next;
          c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next);
          if (c == CP_LS || c == CP_PS)
            break;
          if (c == -1)
            p++;
          p = p_next;
        } else
          p++;
      }
      goto redo;
    } else if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::DivAssign;
    } else {
      p++;
      token.kind = static_cast<TokenKind>(c);
    }
    break;

  case '\\':
    if (p[1] == 'u') {
      const uint8_t *p1 = p + 1;
      int c1            = parse_escape(&p1, true);
      if (c1 >= 0 && unicode_is_id_start(static_cast<uint32_t>(c1))) {
        c                = c1;
        p                = p1;
        buf_ptr          = p;
        ident_has_escape = true;
        goto has_ident;
      }
    }
    goto def_token;

  case 'a':
  case 'b':
  case 'c':
  case 'd':
  case 'e':
  case 'f':
  case 'g':
  case 'h':
  case 'i':
  case 'j':
  case 'k':
  case 'l':
  case 'm':
  case 'n':
  case 'o':
  case 'p':
  case 'q':
  case 'r':
  case 's':
  case 't':
  case 'u':
  case 'v':
  case 'w':
  case 'x':
  case 'y':
  case 'z':
  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'E':
  case 'F':
  case 'G':
  case 'H':
  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
  case 'O':
  case 'P':
  case 'Q':
  case 'R':
  case 'S':
  case 'T':
  case 'U':
  case 'V':
  case 'W':
  case 'X':
  case 'Y':
  case 'Z':
  case '_':
  case '$':
    p++;
    ident_has_escape = false;
    buf_ptr          = p;
  has_ident:
    if (!parse_ident_token(c, ident_has_escape))
      return false;
    p = buf_ptr;
    break;

  case '#':
    if (!parse_private_name())
      return false;
    break;

  case '.':
    if (p[1] == '.' && p[2] == '.') {
      p += 3;
      token.kind = TokenKind::Ellipsis;
      break;
    }
    if (p[1] >= '0' && p[1] <= '9')
      goto parse_number;
    goto def_token;

  case '0':
    goto parse_number;
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  parse_number:
    if (!parse_number(p))
      return false;
    p = buf_ptr;
    break;

  case '*':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::MulAssign;
    } else if (p[1] == '*') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::PowAssign;
      } else {
        p += 2;
        token.kind = TokenKind::Pow;
      }
    } else
      goto def_token;
    break;

  case '%':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::ModAssign;
    } else
      goto def_token;
    break;

  case '+':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::PlusAssign;
    } else if (p[1] == '+') {
      p += 2;
      token.kind = TokenKind::Inc;
    } else
      goto def_token;
    break;

  case '-':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::MinusAssign;
    } else if (p[1] == '-') {
      if (allow_html_comments && p[2] == '>' && (got_lf || last_ptr == buf_start))
        goto skip_line_comment;
      p += 2;
      token.kind = TokenKind::Dec;
    } else
      goto def_token;
    break;

  case '<':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::Lte;
    } else if (p[1] == '<') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::ShlAssign;
      } else {
        p += 2;
        token.kind = TokenKind::Shl;
      }
    } else if (allow_html_comments && p[1] == '!' && p[2] == '-' && p[3] == '-')
      goto skip_line_comment;
    else
      goto def_token;
    break;

  case '>':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::Gte;
    } else if (p[1] == '>') {
      if (p[2] == '>') {
        if (p[3] == '=') {
          p += 4;
          token.kind = TokenKind::ShrAssign;
        } else {
          p += 3;
          token.kind = TokenKind::Shr;
        }
      } else if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::SarAssign;
      } else {
        p += 2;
        token.kind = TokenKind::Sar;
      }
    } else
      goto def_token;
    break;

  case '=':
    if (p[1] == '=') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::StrictEq;
      } else {
        p += 2;
        token.kind = TokenKind::Eq;
      }
    } else if (p[1] == '>') {
      p += 2;
      token.kind = TokenKind::Arrow;
    } else
      goto def_token;
    break;

  case '!':
    if (p[1] == '=') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::StrictNeq;
      } else {
        p += 2;
        token.kind = TokenKind::Neq;
      }
    } else
      goto def_token;
    break;

  case '&':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::AndAssign;
    } else if (p[1] == '&') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::LandAssign;
      } else {
        p += 2;
        token.kind = TokenKind::Land;
      }
    } else
      goto def_token;
    break;

  case '^':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::XorAssign;
    } else
      goto def_token;
    break;

  case '|':
    if (p[1] == '=') {
      p += 2;
      token.kind = TokenKind::OrAssign;
    } else if (p[1] == '|') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::LorAssign;
      } else {
        p += 2;
        token.kind = TokenKind::Lor;
      }
    } else
      goto def_token;
    break;

  case '?':
    if (p[1] == '?') {
      if (p[2] == '=') {
        p += 3;
        token.kind = TokenKind::DoubleQuestionMarkAssign;
      } else {
        p += 2;
        token.kind = TokenKind::DoubleQuestionMark;
      }
    } else if (p[1] == '.' && !(p[2] >= '0' && p[2] <= '9')) {
      p += 2;
      token.kind = TokenKind::QuestionMarkDot;
    } else
      goto def_token;
    break;

  default:
    if (c >= 128) {
      const uint8_t *p_next;
      c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next);
      switch (c) {
      case CP_PS:
      case CP_LS:
        p = p_next;
        goto line_terminator;
      default:
        if (lre_is_space(static_cast<uint32_t>(c))) {
          p = p_next;
          goto redo;
        } else if (unicode_is_id_start(static_cast<uint32_t>(c))) {
          p                = p_next;
          buf_ptr          = p;
          ident_has_escape = false;
          goto has_ident;
        } else
          return fail(LexErrorKind::UnexpectedChar, p);
      }
    }
  def_token:
    token.kind = static_cast<TokenKind>(c);
    p++;
    break;
  }

  buf_ptr = p;
  error_  = LexError{};
  return true;
}

// ─── Identifier parsing ─────────────────────────────────────────────────────

bool Lexer::parse_ident_token(int first_c, bool has_escape) {
  const uint8_t *p  = buf_ptr;
  const uint8_t *p1;
  int c             = first_c;

  if (!has_escape) {
    for (;;) {
      p1 = p;
      c  = *p1++;
      if (c == '\\' && *p1 == 'u') {
        has_escape = true;
        break;
      }
      if (c >= 128) {
        c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
        if (!unicode_is_id_continue(static_cast<uint32_t>(c)))
          break;
      } else if (!unicode_is_id_continue(static_cast<uint32_t>(c))) {
        break;
      }
      p = p1;
    }
  }

  if (!has_escape) {
    auto sv = std::string_view(reinterpret_cast<const char *>(token.ptr),
                               static_cast<size_t>(p - token.ptr));
    buf_ptr = p;

    auto it = kKeywordTable.find(sv);
    if (it != kKeywordTable.end()) {
      token.kind              = it->second;
      token.ident_atom        = e_->intern(sv);
      token.ident_has_escape  = false;
      token.ident_is_reserved = false;
      return true;
    }

    token.ident_atom        = e_->intern(sv);
    token.ident_has_escape  = false;
    token.ident_is_reserved = false;
    token.kind              = TokenKind::Identifier;
    return true;
  }

  // Escape path — must allocate
  p = buf_ptr;
  c = first_c;
  std::string ident_buf;

  for (;;) {
    p1 = p;
    append_utf8(ident_buf, static_cast<unsigned>(c));

    c = *p1++;
    if (c == '\\' && *p1 == 'u') {
      c = parse_escape(&p1, true);
    } else if (c >= 128) {
      c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
    }
    if (!unicode_is_id_continue(static_cast<uint32_t>(c)))
      break;
    p = p1;
  }

  buf_ptr = p;

  token.ident_atom        = e_->intern(ident_buf);
  token.ident_has_escape  = true;
  token.ident_is_reserved = false;
  token.kind              = TokenKind::Identifier;
  return true;
}

bool Lexer::parse_private_name() {
  const uint8_t *p = buf_ptr;
  const uint8_t *p1;
  int c;

  p++; // skip '#'
  p1 = p;
  c  = *p1++;
  bool has_escape = false;
  if (c == '\\' && *p1 == 'u') {
    c = parse_escape(&p1, true);
    has_escape = true;
  } else if (c >= 128) {
    c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
  }
  if (!unicode_is_id_start(static_cast<uint32_t>(c)))
    return fail(LexErrorKind::InvalidPrivateName, p);

  p = p1;

  if (!has_escape) {
    for (;;) {
      p1 = p;
      c  = *p1++;
      if (c == '\\' && *p1 == 'u') {
        has_escape = true;
        break;
      }
      if (c >= 128) {
        c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
        if (!unicode_is_id_continue(static_cast<uint32_t>(c)))
          break;
      } else if (!unicode_is_id_continue(static_cast<uint32_t>(c))) {
        break;
      }
      p = p1;
    }
  }

  if (!has_escape) {
    auto sv = std::string_view(reinterpret_cast<const char *>(token.ptr),
                               static_cast<size_t>(p - token.ptr));
    buf_ptr = p;
    token.ident_atom        = e_->intern(sv);
    token.ident_has_escape  = false;
    token.ident_is_reserved = false;
    token.kind              = TokenKind::PrivateName;
    return true;
  }

  std::string ident_buf;
  ident_buf.push_back('#');
  p = p1;
  append_utf8(ident_buf, static_cast<unsigned>(c));

  for (;;) {
    p1 = p;
    c  = *p1++;
    if (c == '\\' && *p1 == 'u') {
      c = parse_escape(&p1, true);
    } else if (c >= 128) {
      c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
    }
    if (!unicode_is_id_continue(static_cast<uint32_t>(c)))
      break;
    p = p1;
    append_utf8(ident_buf, static_cast<unsigned>(c));
  }

  buf_ptr = p;
  token.ident_atom        = e_->intern(ident_buf);
  token.ident_has_escape  = true;
  token.ident_is_reserved = false;
  token.kind              = TokenKind::PrivateName;
  return true;
}

// ─── String parsing ─────────────────────────────────────────────────────────

bool Lexer::parse_string(int sep, bool do_throw, const uint8_t *p, Token *out, const uint8_t **pp) {
  std::string buf;
  buf.reserve(32);
  uint32_t c;

  for (;;) {
    if (p >= buf_end)
      goto invalid_char;
    c = *p;
    if (c < 0x20) {
      if (sep == '`') {
        if (c == '\r') {
          if (p[1] == '\n')
            p++;
          c = '\n';
        }
      } else if (c == '\n' || c == '\r')
        goto invalid_char;
    }
    p++;
    if (c == static_cast<unsigned>(sep))
      break;
    if (c == '$' && *p == '{' && sep == '`') {
      p++;
      break;
    }
    if (c == '\\') {
      c = *p;
      switch (c) {
      case '\0':
        if (p >= buf_end)
          goto invalid_char;
        p++;
        break;
      case '\'':
      case '\"':
      case '\\':
        p++;
        break;
      case '\r':
        if (p[1] == '\n')
          p++; // fall through
      case '\n':
        p++;
        continue;
      default:
        if (c >= '0' && c <= '9') {
          if (c == '0' && !(p[1] >= '0' && p[1] <= '9')) {
            p++;
            c = '\0';
          } else {
            if (do_throw)
              return fail(LexErrorKind::InvalidOctalEscape, p);
            goto fail;
          }
        } else if (c >= 0x80) {
          const uint8_t *p_next;
          c = static_cast<uint32_t>(unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next));
          if (c > 0x10FFFF) {
            if (do_throw)
              return fail(LexErrorKind::InvalidUtf8, p);
            goto fail;
          }
          p = p_next;
          if (c == CP_LS || c == CP_PS)
            continue;
        } else {
          int ret = parse_escape(&p, true);
          if (ret == -1) {
            if (do_throw)
              return fail(LexErrorKind::InvalidEscape, p);
            goto fail;
          } else if (ret < 0)
            p++;
          else
            c = static_cast<uint32_t>(ret);
        }
        break;
      }
    } else if (c >= 0x80) {
      const uint8_t *p_next;
      c = static_cast<uint32_t>(unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next));
    if (c > 0x10FFFF) {
      if (do_throw)
        return fail(LexErrorKind::InvalidUtf8, p - 1);
      goto fail;
    }
    p = p_next;
  }
  append_utf8(buf, c);
  }

  out->str_val = std::move(buf);
  out->str_len = static_cast<uint32_t>(out->str_val.size());
  out->str_sep = static_cast<int>(c);
  out->kind    = TokenKind::StringLit;
  *pp          = p;
  return true;

invalid_char:
  if (do_throw)
    return fail(LexErrorKind::UnterminatedString, p);
fail:
  return false;
}

// ─── Template literal part ──────────────────────────────────────────────────

bool Lexer::parse_template_part(const uint8_t *p) {
  std::string buf;
  buf.reserve(32);
  uint32_t c;

  for (;;) {
    if (p >= buf_end)
      return fail(LexErrorKind::UnterminatedTemplate, p);
    c = *p++;
    if (c == '`')
      break;
    if (c == '$' && *p == '{') {
      p++;
      break;
    }
    if (c == '\\') {
      if (p >= buf_end)
        return fail(LexErrorKind::UnterminatedTemplate, p);
      c = *p;
      switch (c) {
      case '\0':
        if (p >= buf_end)
          return fail(LexErrorKind::UnterminatedTemplate, p);
        p++;
        c = '\0';
        break;
      case '\'':
      case '\"':
      case '\\':
        p++;
        break;
      case '\r':
        if (p[1] == '\n')
          p++;
        // fall through
      case '\n':
        p++;
        c = '\n';
        append_utf8(buf, c);
        continue;
      default:
        if (c >= '0' && c <= '9') {
          if (c == '0' && !(p[1] >= '0' && p[1] <= '9')) {
            p++;
            c = '\0';
          } else {
            return fail(LexErrorKind::InvalidOctalEscape, p);
          }
        } else if (c >= 0x80) {
          const uint8_t *p_next;
          c = static_cast<uint32_t>(unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next));
          if (c > 0x10FFFF)
            return fail(LexErrorKind::InvalidUtf8, p);
          p = p_next;
          if (c == CP_LS || c == CP_PS) {
            c = '\n';
            append_utf8(buf, c);
            continue;
          }
        } else {
          int ret = parse_escape(&p, true);
          if (ret == -1)
            return fail(LexErrorKind::InvalidEscape, p);
          else if (ret < 0)
            p++;
          else
            c = static_cast<uint32_t>(ret);
        }
        break;
      }
    } else if (c == '\r') {
      if (*p == '\n')
        p++;
      c = '\n';
    } else if (c >= 0x80) {
      const uint8_t *p_next;
      c = static_cast<uint32_t>(unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next));
      if (c > 0x10FFFF)
        return fail(LexErrorKind::InvalidUtf8, p - 1);
      p = p_next;
    }
    append_utf8(buf, c);
  }

  token.str_val = std::move(buf);
  token.str_len = static_cast<uint32_t>(token.str_val.size());
  token.str_sep = static_cast<int>(c);
  token.kind    = TokenKind::TemplateLit;
  buf_ptr       = p;
  return true;
}

// ─── RegExp parsing ─────────────────────────────────────────────────────────

bool Lexer::parse_regexp() {
  const uint8_t *p = buf_ptr;
  bool in_class    = false;
  uint32_t c;

  p++; // skip opening '/'
  const uint8_t *body_start = p;

  for (;;) {
    if (p >= buf_end)
      return fail(LexErrorKind::UnterminatedRegex, p);
    c = *p++;
    if (c == '\n' || c == '\r')
      return fail(LexErrorKind::UnterminatedRegex, p - 1);
    else if (c == '/') {
      if (!in_class)
        break;
    } else if (c == '[')
      in_class = true;
    else if (c == ']')
      in_class = false;
    else if (c == '\\') {
      c = *p++;
      if (c == '\n' || c == '\r')
        return fail(LexErrorKind::UnterminatedRegex, p - 1);
      else if (c == '\0' && p >= buf_end)
        return fail(LexErrorKind::UnterminatedRegex, p - 1);
      else if (c >= 0x80) {
        const uint8_t *p_next;
        c = static_cast<uint32_t>(unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next));
        if (c > 0x10FFFF)
          return fail(LexErrorKind::InvalidUtf8, p - 1);
        p = p_next;
        if (c == CP_LS || c == CP_PS)
          return fail(LexErrorKind::UnterminatedRegex, p - 1);
      }
    } else if (c >= 0x80) {
      const uint8_t *p_next;
      c = static_cast<uint32_t>(unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next));
      if (c > 0x10FFFF)
        return fail(LexErrorKind::InvalidUtf8, p - 1);
      if (c == CP_LS || c == CP_PS)
        return fail(LexErrorKind::UnterminatedRegex, p - 1);
      p = p_next;
    }
  }

  const uint8_t *body_end = p - 1; // p points past the closing '/'

  // flags — verbatim source slice
  const uint8_t *flags_start = p;
  for (;;) {
    const uint8_t *p_next = p;
    c                     = *p_next++;
    if (c >= 0x80) {
      c = static_cast<uint32_t>(unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next));
      if (c > 0x10FFFF) {
        p++;
        return fail(LexErrorKind::InvalidUtf8, p);
      }
    }
    if (!unicode_is_id_continue(c))
      break;
    p = p_next;
  }

  token.regexp_body  = std::string_view(reinterpret_cast<const char *>(body_start),
                                        static_cast<size_t>(body_end - body_start));
  token.regexp_flags = std::string_view(reinterpret_cast<const char *>(flags_start),
                                        static_cast<size_t>(p - flags_start));
  token.kind = TokenKind::RegexpLit;
  buf_ptr    = p;
  return true;
}

// Called by the parser when a '/' token might actually be a regexp literal.
// Rewinds to the start of the '/' and tries to parse as a regexp.
bool Lexer::reparse_as_regexp() {
  buf_ptr = token.ptr; // rewind to start of the '/' token (not last_ptr which may include preceding whitespace)
  return parse_regexp();
}

// ─── Number parsing ─────────────────────────────────────────────────────────

bool Lexer::parse_number(const uint8_t *p) {
  if (p[0] == '0') {
    if (p[1] == 'x' || p[1] == 'X') {
      unsigned long long val = 0;
      p += 2;
      bool has_digit = false;
      for (;;) {
        if (*p == '_') {
          p++;
          continue;
        }
        int d = from_hex(*p);
        if (d < 0)
          break;
        val       = val * 16 + static_cast<unsigned long long>(d);
        has_digit = true;
        p++;
      }
      if (!has_digit)
        return fail(LexErrorKind::InvalidNumber, p);
      token.num_val = static_cast<double>(val);
      token.kind    = TokenKind::Number;
      buf_ptr       = p;
      return true;
    }
    if (p[1] == 'o' || p[1] == 'O') {
      unsigned long long val = 0;
      p += 2;
      bool has_digit = false;
      while (is_digit(*p) || *p == '_') {
        if (*p == '_') {
          p++;
          continue;
        }
        if (*p < '0' || *p > '7')
          break;
        val       = val * 8 + static_cast<unsigned long long>(*p - '0');
        has_digit = true;
        p++;
      }
      if (!has_digit)
        return fail(LexErrorKind::InvalidNumber, p);
      token.num_val = static_cast<double>(val);
      token.kind    = TokenKind::Number;
      buf_ptr       = p;
      return true;
    }
    if (p[1] == 'b' || p[1] == 'B') {
      unsigned long long val = 0;
      p += 2;
      bool has_digit = false;
      while ((*p >= '0' && *p <= '1') || *p == '_') {
        if (*p == '_') {
          p++;
          continue;
        }
        val       = val * 2 + static_cast<unsigned long long>(*p - '0');
        has_digit = true;
        p++;
      }
      if (!has_digit)
        return fail(LexErrorKind::InvalidNumber, p);
      token.num_val = static_cast<double>(val);
      token.kind    = TokenKind::Number;
      buf_ptr       = p;
      return true;
    }
  }

  const uint8_t *start = p;
  bool has_sep = false;
  char buf[128];
  size_t len = 0;
  for (;;) {
    uint8_t c = *p;
    if (c == '_') {
      has_sep = true;
      p++;
      continue;
    }
    if (len < sizeof(buf) - 1)
      buf[len] = static_cast<char>(c);
    if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
        c == '+' || c == '-') {
      p++;
      len++;
      continue;
    }
    break;
  }
  buf[len] = '\0';

  if (has_sep) {
    if (len >= sizeof(buf) - 1)
      return fail(LexErrorKind::InvalidNumber, start);
    if (buf[0] == '_' || buf[len - 1] == '_')
      return fail(LexErrorKind::InvalidNumber, start);
    for (size_t i = 0; i + 1 < len; i++) {
      if (buf[i] == '_' && buf[i + 1] == '_')
        return fail(LexErrorKind::InvalidNumber, start);
    }
  }

  char *end = nullptr;
  double val = std::strtod(buf, &end);
  if (end == buf)
    return fail(LexErrorKind::InvalidNumber, start);

  size_t consumed = static_cast<size_t>(end - buf);
  const uint8_t *after = start;
  size_t raw = 0;
  while (raw < consumed) {
    if (*after == '_')
      after++;
    else {
      after++;
      raw++;
    }
  }
  if (*after == '_')
    return fail(LexErrorKind::InvalidNumber, after);

  auto *next = after;
  const uint8_t *p_next;
  uint32_t nc = static_cast<uint32_t>(unicode_from_utf8(next, UTF8_CHAR_LEN_MAX, &p_next));
  if (val != val || unicode_is_id_continue(nc))
    return fail(LexErrorKind::InvalidNumber, start);

  token.num_val = val;
  token.kind    = TokenKind::Number;
  buf_ptr       = next;
  return true;
}

// ─── Peek ahead ─────────────────────────────────────────────────────────────

TokenKind Lexer::peek_token(bool no_line_terminator) {
  const uint8_t *p    = buf_ptr;
  const uint8_t *last = p;

  for (;;) {
    int c = *p++;
    last  = p - 1;
    switch (c) {
    case '\r':
    case '\n':
      if (no_line_terminator)
        return static_cast<TokenKind>('\n');
      continue;
    case ' ':
    case '\t':
    case '\v':
    case '\f':
      continue;
    case '/':
      if (*p == '/') {
        if (no_line_terminator)
          return static_cast<TokenKind>('\n');
        while (*p && *p != '\r' && *p != '\n')
          p++;
        continue;
      }
      if (*p == '*') {
        while (*++p) {
          if ((*p == '\r' || *p == '\n') && no_line_terminator)
            return static_cast<TokenKind>('\n');
          if (*p == '*' && p[1] == '/') {
            p += 2;
            break;
          }
        }
        continue;
      }
      return static_cast<TokenKind>(c);
    case '=':
      if (*p == '>')
        return TokenKind::Arrow;
      return static_cast<TokenKind>(c);
    case 'i':
      if (p[0] == 'n' && !unicode_is_id_continue(static_cast<uint32_t>(p[1])))
        return TokenKind::KwIn;
      if (p[0] == 'm' && p[1] == 'p' && p[2] == 'o' && p[3] == 'r' && p[4] == 't' && !unicode_is_id_continue(static_cast<uint32_t>(p[5])))
        return TokenKind::KwImport;
      return TokenKind::Identifier;
    case 'o':
      if (p[0] == 'f' && !unicode_is_id_continue(static_cast<uint32_t>(p[1])))
        return TokenKind::KwOf;
      return TokenKind::Identifier;
    case 'e':
      if (p[0] == 'x' && p[1] == 'p' && p[2] == 'o' && p[3] == 'r' && p[4] == 't' && !unicode_is_id_continue(static_cast<uint32_t>(p[5])))
        return TokenKind::KwExport;
      return TokenKind::Identifier;
    case 'f':
      if (p[0] == 'u' && p[1] == 'n' && p[2] == 'c' && p[3] == 't' && p[4] == 'i' && p[5] == 'o' && p[6] == 'n' &&
          !unicode_is_id_continue(static_cast<uint32_t>(p[7])))
        return TokenKind::KwFunction;
      return TokenKind::Identifier;
    case '\\':
      if (*p == 'u') {
        const uint8_t *p1 = p + 1;
        if (unicode_is_id_start(static_cast<uint32_t>(parse_escape(&p1, true))))
          return TokenKind::Identifier;
      }
      return static_cast<TokenKind>(c);
    default:
      if (c >= 128) {
        const uint8_t *p_next;
        c = unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next);
        p = p_next;
        if (no_line_terminator && (c == CP_PS || c == CP_LS))
          return static_cast<TokenKind>('\n');
      }
      if (lre_is_space(static_cast<uint32_t>(c)))
        continue;
      if (unicode_is_id_start(static_cast<uint32_t>(c)))
        return TokenKind::Identifier;
      return static_cast<TokenKind>(c);
    }
  }
}

// ─── Shebang ────────────────────────────────────────────────────────────────

void Lexer::skip_shebang(const uint8_t **pp, const uint8_t *buf_end) {
  const uint8_t *p = *pp;
  int c;

  if (p[0] == '#' && p[1] == '!') {
    p += 2;
    while (p < buf_end) {
      if (*p == '\n' || *p == '\r')
        break;
      if (*p >= 0x80) {
        c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p);
        if (c == CP_LS || c == CP_PS)
          break;
        if (c == -1)
          p++;
      } else
        p++;
    }
    *pp = p;
  }
}

} // namespace qjsp
