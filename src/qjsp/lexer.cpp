#include "qjsp/lexer.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include "qjsp/unicode_id.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>

namespace qjsp {

// ─── Character helpers ──────────────────────────────────────────────────────

void Lexer::copy_str(std::string &dst, uint32_t &dst_len, const std::string &buf) {
  dst_len = static_cast<uint32_t>(buf.size());
  dst     = buf;
}

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

void Lexer::init(Runtime *rt_val, const char *filename_val, const uint8_t *source, size_t source_len) {
  rt        = rt_val;
  filename  = filename_val;
  buf_start = source;
  buf_ptr   = source;
  buf_end   = source + source_len;
  last_ptr  = source;
  got_lf    = false;
}

void Lexer::reset(const uint8_t *source, size_t source_len) {
  buf_start = source;
  buf_ptr   = source;
  buf_end   = source + source_len;
  last_ptr  = source;
  got_lf    = false;
  token     = Token{};
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
          return false;
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
          return false;
      }
    }
  def_token:
    token.kind = static_cast<TokenKind>(c);
    p++;
    break;
  }

  buf_ptr = p;
  return true;
}

// ─── Identifier parsing ─────────────────────────────────────────────────────

bool Lexer::parse_ident_token(int first_c, bool has_escape) {
  const uint8_t *p = buf_ptr;
  const uint8_t *p1;
  int c = first_c;
  std::string ident_buf;

  for (;;) {
    p1 = p;
    append_utf8(ident_buf, static_cast<unsigned>(c));

    c = *p1++;
    if (c == '\\' && *p1 == 'u') {
      c          = parse_escape(&p1, true);
      has_escape = true;
    } else if (c >= 128) {
      c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
    }
    if (!unicode_is_id_continue(static_cast<uint32_t>(c)))
      break;
    p = p1;
  }

  buf_ptr = p;

  auto *str = StrPrim::allocate_raw(ident_buf);
  if (!str)
    return false;
  Atom atom               = rt->intern_copy(str);
  token.ident_atom        = atom;
  token.ident_has_escape  = has_escape;
  token.ident_is_reserved = false;

  // Keyword detection via lookup table (not atom index ranges)
  if (!has_escape) {
    auto it = kKeywordTable.find(ident_buf);
    if (it != kKeywordTable.end()) {
      token.kind = it->second;
      return true;
    }
  }

  token.kind = TokenKind::Identifier;
  return true;
}

bool Lexer::parse_private_name() {
  const uint8_t *p = buf_ptr;
  const uint8_t *p1;
  int c;

  p++; // skip '#'
  p1 = p;
  c  = *p1++;
  if (c == '\\' && *p1 == 'u') {
    c = parse_escape(&p1, true);
  } else if (c >= 128) {
    c = unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p1);
  }
  if (!unicode_is_id_start(static_cast<uint32_t>(c)))
    return false;

  p = p1;
  std::string ident_buf;
  ident_buf.push_back('#');
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

  buf_ptr   = p;
  auto *str = StrPrim::allocate_raw(ident_buf);
  if (!str)
    return false;
  Atom atom               = rt->intern_copy(str);
  token.ident_atom        = atom;
  token.ident_has_escape  = false;
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
            if (c >= '8' || sep == '`') {
              if (do_throw)
                return false;
              goto fail;
            } else
              goto fail;
          }
        } else if (c >= 0x80) {
          const uint8_t *p_next;
          c = static_cast<uint32_t>(unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next));
          if (c > 0x10FFFF) {
            if (do_throw)
              return false;
            goto fail;
          }
          p = p_next;
          if (c == CP_LS || c == CP_PS)
            continue;
        } else {
          int ret = parse_escape(&p, true);
          if (ret == -1) {
            if (do_throw)
              return false;
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
          return false;
        goto fail;
      }
      p = p_next;
    }
    append_utf8(buf, c);
  }

  copy_str(out->str_val, out->str_len, buf);
  out->str_sep = static_cast<int>(c);
  out->kind    = TokenKind::StringLit;
  *pp          = p;
  return true;

invalid_char:
  if (do_throw)
    return false;
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
      return false;
    c = *p++;
    if (c == '`')
      break;
    if (c == '$' && *p == '{') {
      p++;
      break;
    }
    if (c == '\\') {
      if (p >= buf_end)
        return false;
      c = *p;
      switch (c) {
      case '\0':
        if (p >= buf_end)
          return false;
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
            return false;
          }
        } else if (c >= 0x80) {
          const uint8_t *p_next;
          c = static_cast<uint32_t>(unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next));
          if (c > 0x10FFFF)
            return false;
          p = p_next;
          if (c == CP_LS || c == CP_PS) {
            c = '\n';
            append_utf8(buf, c);
            continue;
          }
        } else {
          int ret = parse_escape(&p, true);
          if (ret == -1)
            return false;
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
        return false;
      p = p_next;
    }
    append_utf8(buf, c);
  }

  copy_str(token.str_val, token.str_len, buf);
  token.str_sep = static_cast<int>(c);
  token.kind    = TokenKind::TemplateLit;
  buf_ptr       = p;
  return true;
}

// ─── RegExp parsing ─────────────────────────────────────────────────────────

bool Lexer::parse_regexp() {
  const uint8_t *p = buf_ptr;
  bool in_class    = false;
  std::string body, flags;
  uint32_t c;

  p++; // skip opening '/'

  for (;;) {
    if (p >= buf_end)
      return false;
    c = *p++;
    if (c == '\n' || c == '\r')
      return false;
    else if (c == '/') {
      if (!in_class)
        break;
    } else if (c == '[')
      in_class = true;
    else if (c == ']')
      in_class = false;
    else if (c == '\\') {
      body.push_back('\\');
      c = *p++;
      if (c == '\n' || c == '\r')
        return false;
      else if (c == '\0' && p >= buf_end)
        return false;
      else if (c >= 0x80) {
        const uint8_t *p_next;
        c = static_cast<uint32_t>(unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next));
        if (c > 0x10FFFF)
          return false;
        p = p_next;
        if (c == CP_LS || c == CP_PS)
          return false;
      }
    } else if (c >= 0x80) {
      const uint8_t *p_next;
      c = static_cast<uint32_t>(unicode_from_utf8(p - 1, UTF8_CHAR_LEN_MAX, &p_next));
      if (c > 0x10FFFF)
        return false;
      if (c == CP_LS || c == CP_PS)
        return false;
      p = p_next;
    }
    body.push_back(static_cast<char>(c));
  }

  // flags
  for (;;) {
    const uint8_t *p_next = p;
    c                     = *p_next++;
    if (c >= 0x80) {
      c = static_cast<uint32_t>(unicode_from_utf8(p, UTF8_CHAR_LEN_MAX, &p_next));
      if (c > 0x10FFFF) {
        p++;
        return false;
      }
    }
    if (!unicode_is_id_continue(c))
      break;
    flags.push_back(static_cast<char>(c));
    p = p_next;
  }

  copy_str(token.regexp_body, token.regexp_body_len, body);
  copy_str(token.regexp_flags, token.regexp_flags_len, flags);
  token.kind = TokenKind::RegexpLit;
  buf_ptr    = p;
  return true;
}

// Called by the parser when a '/' token might actually be a regexp literal.
// Rewinds to the start of the '/' and tries to parse as a regexp.
bool Lexer::reparse_as_regexp() {
  buf_ptr = last_ptr; // rewind to start of '/'
  return parse_regexp();
}

// ─── Number parsing ─────────────────────────────────────────────────────────

bool Lexer::parse_number(const uint8_t *p) {
  const char *c_str = reinterpret_cast<const char *>(p);
  char *end         = nullptr;

  if (p[0] == '0') {
    if (p[1] == 'x' || p[1] == 'X') {
      double val = std::strtod(c_str, &end);
      if (end == c_str)
        return false;
      token.num_val = val;
      token.kind    = TokenKind::Number;
      buf_ptr       = reinterpret_cast<const uint8_t *>(end);
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
        return false;
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
        return false;
      token.num_val = static_cast<double>(val);
      token.kind    = TokenKind::Number;
      buf_ptr       = p;
      return true;
    }
  }

  double val = std::strtod(c_str, &end);
  if (end == c_str)
    return false;

  auto *next = reinterpret_cast<const uint8_t *>(end);
  const uint8_t *p_next;
  uint32_t nc = static_cast<uint32_t>(unicode_from_utf8(next, UTF8_CHAR_LEN_MAX, &p_next));
  if (val != val || unicode_is_id_continue(nc))
    return false;

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
