#include "qjsp/lexer2.hpp"
#include "qjsp/unicode_id.hpp"
#include <cctype>
#include <cstring>

namespace qjsp {

// ─── Classification tables (comptime-equivalent: static constexpr) ─────────

static constexpr bool kIdentStart[256] = {
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false, true,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
     true, true, true, true, true, true, true, true, true, true, true,false,false,false,false, true,
    false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
     true, true, true, true, true, true, true, true, true, true, true,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
};

static constexpr bool kIdentCont[256] = {
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false, true,false,false,false,false,false,false,false,false,false,false,false,
     true, true, true, true, true, true, true, true, true, true,false,false,false,false,false,false,
    false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
     true, true, true, true, true, true, true, true, true, true, true,false,false,false,false, true,
    false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true,
     true, true, true, true, true, true, true, true, true, true, true,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
    false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,
};

// ─── Init ────────────────────────────────────────────────────────────────────

void Lexer2::init(const uint8_t *source, uint32_t source_len, bool is_module) {
    src_ = source;
    len_ = source_len;
    cur_ = 0;
    flags_ = 0;
    is_module_ = is_module;
    hashbang = {0, 0};
    skip_hashbang();
}

// ─── Peek ────────────────────────────────────────────────────────────────────

inline uint8_t Lexer2::peek(uint32_t offset) const {
    return src_[cur_ + offset];
}

inline Token Lexer2::make(TokenTag tag, uint32_t start, uint32_t end) const {
    return Token{start, end, tag, flags_};
}

// ─── Hashbang ────────────────────────────────────────────────────────────────

void Lexer2::skip_hashbang() {
    if (src_[cur_] == '#' && src_[cur_ + 1] == '!') {
        uint32_t start = cur_;
        cur_ += 2;
        while (src_[cur_] != 0) {
            uint8_t c = src_[cur_];
            if (c == '\n' || c == '\r') break;
            if (c >= 0x80) {
                uint32_t cp;
                uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
                if (cp == 0x2028 || cp == 0x2029) break;
                cur_ += adv;
            } else {
                cur_++;
            }
        }
        hashbang = {start, cur_};
    }
}

// ─── Escape sequence skipper ─────────────────────────────────────────────────
//
// Advances cur_ past one escape sequence starting after '\'. Sets HasEscape
// and InvalidEscape as appropriate. Returns true if the escape was valid.

void Lexer2::skip_escape() {
    if (src_[cur_] == 0) { flags_ |= TF::InvalidEscape; return; }
    uint8_t c = src_[cur_];
    switch (c) {
    case '\n':
        cur_++;
        if (src_[cur_] == '\r') cur_++;
        break;
    case '\r':
        cur_++;
        break;
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
        cur_++;
        if (src_[cur_] >= '0' && src_[cur_] <= '7') cur_++;
        if (src_[cur_] >= '0' && src_[cur_] <= '7') cur_++;
        break;
    case 'x': case 'X':
        cur_++;
        if (std::isxdigit(src_[cur_]) && std::isxdigit(src_[cur_ + 1])) {
            cur_ += 2;
        } else {
            flags_ |= TF::InvalidEscape;
        }
        break;
    case 'u':
        cur_++;
        if (src_[cur_] == '{') {
            cur_++;
            bool found = false;
            while (src_[cur_] != 0 && src_[cur_] != '}') {
                if (!std::isxdigit(src_[cur_]) && src_[cur_] != '_') {
                    flags_ |= TF::InvalidEscape;
                }
                cur_++;
                found = true;
            }
            if (src_[cur_] != 0) cur_++;
            if (!found) flags_ |= TF::InvalidEscape;
        } else {
            if (std::isxdigit(src_[cur_]) && std::isxdigit(src_[cur_ + 1]) &&
                std::isxdigit(src_[cur_ + 2]) && std::isxdigit(src_[cur_ + 3])) {
                cur_ += 4;
            } else {
                flags_ |= TF::InvalidEscape;
                cur_ += 4;
            }
        }
        break;
    default:
        cur_++;
        break;
    }
}

TokenTag Lexer2::classify_keyword(const uint8_t *s, uint32_t n) const {
    switch (n) {
    case 2:
        if (s[0]=='d' && s[1]=='o')   return tok_do;
        if (s[0]=='i' && s[1]=='f')   return tok_if;
        if (s[0]=='i' && s[1]=='n')   return tok_in;
        if (s[0]=='a' && s[1]=='s')   return tok_as;
        if (s[0]=='o' && s[1]=='f')   return tok_of;
        break;
    case 3:
        switch (s[0]) {
        case 'f': if (s[1]=='o' && s[2]=='r') return tok_for; break;
        case 'n': if (s[1]=='e' && s[2]=='w') return tok_new; break;
        case 't': if (s[1]=='r' && s[2]=='y') return tok_try; break;
        case 'v': if (s[1]=='a' && s[2]=='r') return tok_var; break;
        case 'l': if (s[1]=='e' && s[2]=='t') return tok_let; break;
        case 'g': if (s[1]=='e' && s[2]=='t') return tok_get; break;
        case 's': if (s[1]=='e' && s[2]=='t') return tok_set; break;
        }
        break;
    case 4:
        switch (s[0]) {
        case 'c': if (s[1]=='a' && s[2]=='s' && s[3]=='e')   return tok_case; break;
        case 'e': if (s[1]=='l' && s[2]=='s' && s[3]=='e')   return tok_else;
                  if (s[1]=='n' && s[2]=='u' && s[3]=='m')   return tok_enum; break;
        case 'f': if (s[1]=='r' && s[2]=='o' && s[3]=='m')   return tok_from; break;
        case 'n': if (s[1]=='u' && s[2]=='l' && s[3]=='l')   return tok_null; break;
        case 't': if (s[1]=='h' && s[2]=='i' && s[3]=='s')   return tok_this;
                  if (s[1]=='r' && s[2]=='u' && s[3]=='e')   return tok_true; break;
        case 'v': if (s[1]=='o' && s[2]=='i' && s[3]=='d')   return tok_void; break;
        case 'w': if (s[1]=='i' && s[2]=='t' && s[3]=='h')   return tok_with; break;
        }
        break;
    case 5:
        switch (s[0]) {
        case 'a': if (s[1]=='w' && s[2]=='a' && s[3]=='i' && s[4]=='t') return tok_await;
                  if (s[1]=='s' && s[2]=='y' && s[3]=='n' && s[4]=='c') return tok_async; break;
        case 'b': if (s[1]=='r' && s[2]=='e' && s[3]=='a' && s[4]=='k') return tok_break; break;
        case 'c': if (s[1]=='a' && s[2]=='t' && s[3]=='c' && s[4]=='h') return tok_catch;
                  if (s[1]=='o' && s[2]=='n' && s[3]=='s' && s[4]=='t') return tok_const;
                  if (s[1]=='l' && s[2]=='a' && s[3]=='s' && s[4]=='s') return tok_class; break;
        case 'f': if (s[1]=='a' && s[2]=='l' && s[3]=='s' && s[4]=='e') return tok_false; break;
        case 's': if (s[1]=='u' && s[2]=='p' && s[3]=='e' && s[4]=='r') return tok_super; break;
        case 't': if (s[1]=='h' && s[2]=='r' && s[3]=='o' && s[4]=='w') return tok_throw; break;
        case 'w': if (s[1]=='h' && s[2]=='i' && s[3]=='l' && s[4]=='e') return tok_while; break;
        case 'y': if (s[1]=='i' && s[2]=='e' && s[3]=='l' && s[4]=='d') return tok_yield; break;
        }
        break;
    case 6:
        switch (s[0]) {
        case 'd': if (s[1]=='e' && s[2]=='l' && s[3]=='e' && s[4]=='t' && s[5]=='e') return tok_delete; break;
        case 'e': if (s[1]=='x' && s[2]=='p' && s[3]=='o' && s[4]=='r' && s[5]=='t') return tok_export; break;
        case 'i': if (s[1]=='m' && s[2]=='p' && s[3]=='o' && s[4]=='r' && s[5]=='t') return tok_import; break;
        case 'p': if (s[1]=='u' && s[2]=='b' && s[3]=='l' && s[4]=='i' && s[5]=='c') return tok_public; break;
        case 'r': if (s[1]=='e' && s[2]=='t' && s[3]=='u' && s[4]=='r' && s[5]=='n') return tok_return; break;
        case 's': if (s[1]=='w' && s[2]=='i' && s[3]=='t' && s[4]=='c' && s[5]=='h') return tok_switch;
                  if (s[1]=='t' && s[2]=='a' && s[3]=='t' && s[4]=='i' && s[5]=='c') return tok_static; break;
        case 't': if (s[1]=='y' && s[2]=='p' && s[3]=='e' && s[4]=='o' && s[5]=='f') return tok_typeof; break;
        }
        break;
    case 7:
        switch (s[0]) {
        case 'd': if (s[1]=='e' && s[2]=='f' && s[3]=='a' && s[4]=='u' && s[5]=='l' && s[6]=='t') return tok_default; break;
        case 'e': if (s[1]=='x' && s[2]=='t' && s[3]=='e' && s[4]=='n' && s[5]=='d' && s[6]=='s') return tok_extends; break;
        case 'f': if (s[1]=='i' && s[2]=='n' && s[3]=='a' && s[4]=='l' && s[5]=='l' && s[6]=='y') return tok_finally; break;
        case 'p': if (s[1]=='r' && s[2]=='i' && s[3]=='v' && s[4]=='a' && s[5]=='t' && s[6]=='e') return tok_private;
                  if (s[1]=='a' && s[2]=='c' && s[3]=='k' && s[4]=='a' && s[5]=='g' && s[6]=='e') return tok_package; break;
        }
        break;
    case 8:
        switch (s[0]) {
        case 'c': if (s[1]=='o' && s[2]=='n' && s[3]=='t' && s[4]=='i' && s[5]=='n' && s[6]=='u' && s[7]=='e') return tok_continue; break;
        case 'f': if (s[1]=='u' && s[2]=='n' && s[3]=='c' && s[4]=='t' && s[5]=='i' && s[6]=='o' && s[7]=='n') return tok_function; break;
        case 'd': if (s[1]=='e' && s[2]=='b' && s[3]=='u' && s[4]=='g' && s[5]=='g' && s[6]=='e' && s[7]=='r') return tok_debugger; break;
        }
        break;
    case 9:
        if (s[0]=='p' && s[1]=='r' && s[2]=='o' && s[3]=='t' && s[4]=='e' && s[5]=='c' && s[6]=='t' && s[7]=='e' && s[8]=='d') return tok_protected;
        if (s[0]=='i' && s[1]=='n' && s[2]=='t' && s[3]=='e' && s[4]=='r' && s[5]=='f' && s[6]=='a' && s[7]=='c' && s[8]=='e') return tok_interface;
        break;
    case 10:
        if (s[0]=='i' && s[1]=='n' && s[2]=='s' && s[3]=='t' && s[4]=='a' && s[5]=='n' && s[6]=='c' && s[7]=='e' && s[8]=='o' && s[9]=='f') return tok_instanceof;
        if (s[0]=='i' && s[1]=='m' && s[2]=='p' && s[3]=='l' && s[4]=='e' && s[5]=='m' && s[6]=='e' && s[7]=='n' && s[8]=='t' && s[9]=='s') return tok_implements;
        break;
    case 11:
        if (s[0]=='c' && s[1]=='o' && s[2]=='n' && s[3]=='s' && s[4]=='t' && s[5]=='r' && s[6]=='u' && s[7]=='c' && s[8]=='t' && s[9]=='o' && s[10]=='r') return tok_constructor;
        break;
    }
    return tok_ident;
}

// ─── UTF-8 decode ────────────────────────────────────────────────────────────

uint32_t Lexer2::decode_utf8(const uint8_t *p, uint32_t max_len, uint32_t &cp) {
    uint8_t b0 = *p;
    if (b0 < 0x80) { cp = b0; return 1; }
    if (b0 < 0xC0) { cp = b0; return 1; } // invalid, skip 1
    uint32_t len;
    uint32_t mincp;
    if (b0 < 0xE0)      { len = 2; mincp = 0x80;  }
    else if (b0 < 0xF0) { len = 3; mincp = 0x800; }
    else                 { len = 4; mincp = 0x10000; }
    if (max_len < len) { cp = b0; return 1; }
    uint32_t val = b0 & (0xFF >> (len + 1));
    for (uint32_t i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) { cp = b0; return 1; }
        val = (val << 6) | (p[i] & 0x3F);
    }
    if (val < mincp) { cp = b0; return 1; }
    cp = val;
    return len;
}

// ─── Whitespace and comment skip ─────────────────────────────────────────────

void Lexer2::skip_ws_and_comments() {
    while (src_[cur_] != 0) {
        uint8_t c = src_[cur_];
        switch (c) {
        case ' ': case '\t': case '\f': case '\v':
            cur_++;
            break;
        case '\n':
            flags_ |= TF::NewlineBefore;
            cur_++;
            break;
        case '\r':
            flags_ |= TF::NewlineBefore;
            cur_++;
            if (src_[cur_] == '\n') cur_++;
            break;
        case '/':
            if (src_[cur_ + 1] == '/') {
                cur_ += 2;
                while (src_[cur_] != 0) {
                    uint8_t ch = src_[cur_];
                    if (ch == '\n' || ch == '\r') break;
                    if (ch >= 0x80) {
                        uint32_t cp;
                        uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
                        if (cp == 0x2028 || cp == 0x2029) break;
                        cur_ += adv;
                    } else {
                        cur_++;
                    }
                }
                break;
            }
            if (src_[cur_ + 1] == '*') {
                cur_ += 2;
                while (src_[cur_] != 0) {
                    if (src_[cur_] == '*' && src_[cur_ + 1] == '/') { cur_ += 2; break; }
                    if (src_[cur_] == '\n' || src_[cur_] == '\r') flags_ |= TF::NewlineBefore;
                    if (src_[cur_] >= 0x80) {
                        uint32_t cp;
                        uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
                        if (cp == 0x2028 || cp == 0x2029) flags_ |= TF::NewlineBefore;
                        cur_ += adv;
                    } else {
                        cur_++;
                    }
                }
                break;
            }
            return;
        default:
            if (c >= 0x80) {
                uint32_t cp;
                uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
                if (cp == 0x2028 || cp == 0x2029) {
                    flags_ |= TF::NewlineBefore;
                    cur_ += adv;
                    break;
                }
                if (cp == 0xFEFF || cp == 0x00A0 || cp == '\t') {
                    cur_ += adv;
                    break;
                }
            }
            return;
        }
    }
}

// ─── next_token — main dispatch ──────────────────────────────────────────────

Token Lexer2::next_token() {
    flags_ = 0;
    skip_ws_and_comments();

    if (src_[cur_] == 0) return make(tok_eof, cur_, cur_);

    uint8_t c = src_[cur_];
    uint32_t start = cur_;

    // Fast path: ASCII identifier/keyword
    if (kIdentStart[c]) {
        uint32_t pos = cur_ + 1;
        while (kIdentCont[src_[pos]]) pos++;
        if (src_[pos] == 0 || (src_[pos] != '\\' && src_[pos] < 0x80)) {
            cur_ = pos;
            TokenTag tag = classify_keyword(src_ + start, pos - start);
            return make(tag, start, pos);
        }
        return scan_ident_or_keyword();
    }

    // Unicode identifier start
    if (c >= 0x80) {
        uint32_t cp;
        decode_utf8(src_ + cur_, len_ - cur_, cp);
        if (unicode_is_id_start(cp)) {
            return scan_ident_or_keyword();
        }
    }

    // Backslash-u identifier start
    if (c == '\\' && peek(1) == 'u') {
        return scan_ident_or_keyword();
    }

    // Numbers
    if (std::isdigit(c) || (c == '.' && std::isdigit(peek(1)))) {
        return scan_number();
    }

    // Strings
    if (c == '\'' || c == '"') {
        return scan_string();
    }

    // Template
    if (c == '`') {
        return scan_template_part();
    }

    // Private identifier
    if (c == '#' && (kIdentStart[src_[cur_ + 1]] || src_[cur_ + 1] == '\\')) {
        cur_++; // skip '#'
        if (src_[cur_] < 0x80 && kIdentStart[src_[cur_]]) {
            cur_++;
            while (kIdentCont[src_[cur_]]) cur_++;
        } else {
            if (src_[cur_] >= 0x80) {
                uint32_t cp;
                cur_ += decode_utf8(src_ + cur_, len_ - cur_, cp);
            } else if (src_[cur_] == '\\' && peek(1) == 'u') {
                cur_ += 2;
                if (src_[cur_] == '{') {
                    cur_++;
                    while (src_[cur_] != 0 && src_[cur_] != '}') cur_++;
                    if (src_[cur_] != 0) cur_++;
                } else {
                    cur_ += 4;
                }
            }
            while (src_[cur_] != 0) {
                if (kIdentCont[src_[cur_]] && src_[cur_] < 0x80) { cur_++; continue; }
                if (src_[cur_] >= 0x80) {
                    uint32_t cp;
                    uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
                    if (unicode_is_id_continue(cp)) { cur_ += adv; continue; }
                }
                break;
            }
        }
        return make(tok_private_ident, start, cur_);
    }

    return scan_punct();
}

// ─── Identifier scanning ─────────────────────────────────────────────────────

Token Lexer2::scan_ident_or_keyword() {
    uint32_t start = cur_;

    if (src_[cur_] == '\\' && peek(1) == 'u') {
        flags_ |= TF::HasEscape;
        cur_ += 2;
        if (src_[cur_] == '{') {
            cur_++;
            while (src_[cur_] != 0 && src_[cur_] != '}') cur_++;
            if (src_[cur_] != 0) cur_++;
        } else {
            cur_ += 4;
        }
    } else if (src_[cur_] >= 0x80) {
        uint32_t cp;
        cur_ += decode_utf8(src_ + cur_, len_ - cur_, cp);
    } else {
        cur_++;
    }

    while (src_[cur_] != 0) {
        if (kIdentCont[src_[cur_]] && src_[cur_] < 0x80) {
            cur_++;
            continue;
        }
        if (src_[cur_] == '\\' && peek(1) == 'u') {
            flags_ |= TF::HasEscape;
            cur_ += 2;
            if (src_[cur_] == '{') {
                cur_++;
                while (src_[cur_] != 0 && src_[cur_] != '}') cur_++;
                if (src_[cur_] != 0) cur_++;
            } else {
                cur_ += 4;
            }
            continue;
        }
        if (src_[cur_] >= 0x80) {
            uint32_t cp;
            uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
            if (unicode_is_id_continue(cp)) {
                cur_ += adv;
                continue;
            }
        }
        break;
    }

    TokenTag tag = tok_ident;
    if (!(flags_ & TF::HasEscape)) {
        tag = classify_keyword(src_ + start, cur_ - start);
    }
    return make(tag, start, cur_);
}

// ─── Number scanning ─────────────────────────────────────────────────────────

Token Lexer2::scan_number() {
    uint32_t start = cur_;
    uint8_t c = src_[cur_];

    if (c == '0') {
        uint8_t c1 = peek(1);
        if (c1 == 'x' || c1 == 'X') {
            cur_ += 2;
            uint32_t body_start = cur_;
            while (std::isxdigit(src_[cur_]) || src_[cur_] == '_') cur_++;
            return make(cur_ == body_start ? tok_numeric : tok_hex, start, cur_);
        }
        if (c1 == 'o' || c1 == 'O') {
            cur_ += 2;
            uint32_t body_start = cur_;
            while ((src_[cur_] >= '0' && src_[cur_] <= '7') || src_[cur_] == '_') cur_++;
            return make(cur_ == body_start ? tok_numeric : tok_octal, start, cur_);
        }
        if (c1 == 'b' || c1 == 'B') {
            cur_ += 2;
            uint32_t body_start = cur_;
            while ((src_[cur_] == '0' || src_[cur_] == '1') || src_[cur_] == '_') cur_++;
            return make(cur_ == body_start ? tok_numeric : tok_binary, start, cur_);
        }
    }

    bool has_dot = false;
    bool has_exp = false;

    while (std::isdigit(src_[cur_]) || src_[cur_] == '_') cur_++;

    if (src_[cur_] == '.') {
        has_dot = true;
        cur_++;
        while (std::isdigit(src_[cur_]) || src_[cur_] == '_') cur_++;
    }

    if (src_[cur_] == 'e' || src_[cur_] == 'E') {
        has_exp = true;
        cur_++;
        if (src_[cur_] == '+' || src_[cur_] == '-') cur_++;
        uint32_t exp_start = cur_;
        while (std::isdigit(src_[cur_]) || src_[cur_] == '_') cur_++;
        if (cur_ == exp_start) return make(tok_numeric, start, cur_);
    }

    if (src_[cur_] == 'n') {
        if (has_dot || has_exp) return make(tok_numeric, start, cur_);
        cur_++;
        return make(tok_bigint, start, cur_);
    }

    return make(tok_numeric, start, cur_);
}

// ─── String scanning ─────────────────────────────────────────────────────────

Token Lexer2::scan_string() {
    uint32_t start = cur_;
    uint8_t quote = src_[cur_];
    cur_++;

    while (src_[cur_] != 0) {
        uint8_t c = src_[cur_];
        if (c == quote) {
            cur_++;
            return make(tok_string, start, cur_);
        }
        if (c == '\\') {
            flags_ |= TF::HasEscape;
            cur_++;
            skip_escape();
            continue;
        }
        if (c == '\n' || c == '\r') break;
        if (c >= 0x80) {
            uint32_t cp;
            cur_ += decode_utf8(src_ + cur_, len_ - cur_, cp);
            continue;
        }
        cur_++;
    }

    return make(tok_string, start, cur_);
}

// ─── Template scanning ───────────────────────────────────────────────────────

Token Lexer2::scan_template_part() {
    uint32_t start = cur_;
    cur_++;

    while (src_[cur_] != 0) {
        uint8_t c = src_[cur_];
        if (c == '`') {
            cur_++;
            return make(tok_template_full, start, cur_);
        }
        if (c == '\\') {
            flags_ |= TF::HasEscape;
            cur_++;
            skip_escape();
            continue;
        }
        if (c == '$' && src_[cur_ + 1] == '{') {
            cur_ += 2;
            return make(tok_template_head, start, cur_);
        }
        if (c == '\r') {
            if (src_[cur_ + 1] == '\n') cur_++;
            cur_++;
            continue;
        }
        if (c >= 0x80) {
            uint32_t cp;
            cur_ += decode_utf8(src_ + cur_, len_ - cur_, cp);
            continue;
        }
        cur_++;
    }

    return make(tok_template_full, start, cur_);
}

Token Lexer2::rescan_template(uint32_t rbrace_start) {
    cur_ = rbrace_start + 1;
    uint32_t start = rbrace_start;

    while (src_[cur_] != 0) {
        uint8_t c = src_[cur_];
        if (c == '`') {
            cur_++;
            return make(tok_template_tail, start, cur_);
        }
        if (c == '\\') {
            flags_ |= TF::HasEscape;
            cur_++;
            skip_escape();
            continue;
        }
        if (c == '$' && src_[cur_ + 1] == '{') {
            cur_ += 2;
            return make(tok_template_mid, start, cur_);
        }
        if (c == '\r') {
            if (src_[cur_ + 1] == '\n') cur_++;
            cur_++;
            continue;
        }
        if (c >= 0x80) {
            uint32_t cp;
            cur_ += decode_utf8(src_ + cur_, len_ - cur_, cp);
            continue;
        }
        cur_++;
    }

    return make(tok_template_tail, start, cur_);
}

// ─── Regex scanning ──────────────────────────────────────────────────────────

Token Lexer2::scan_regex() {
    uint32_t start = cur_;
    cur_++; // skip '/'

    bool in_class = false;
    while (src_[cur_] != 0) {
        uint8_t c = src_[cur_];
        if (c == '\\') {
            cur_ += 2;
            continue;
        }
        if (c == '[') { in_class = true; cur_++; continue; }
        if (c == ']') { in_class = false; cur_++; continue; }
        if (c == '/' && !in_class) {
            cur_++;
            uint32_t seen = 0;
            while (std::isalpha(src_[cur_])) {
                uint8_t f = src_[cur_];
                if (f != 'g' && f != 'i' && f != 'm' && f != 's' && f != 'u' && f != 'y' && f != 'd' && f != 'v') {
                    break;
                }
                uint32_t bit = 1u << (f - 'a');
                if (seen & bit) break;
                seen |= bit;
                cur_++;
            }
            if ((seen & (1u << ('u' - 'a'))) && (seen & (1u << ('v' - 'a')))) {
            }
            return make(tok_regexp, start, cur_);
        }
        if (c == '\n' || c == '\r') break;
        if (c >= 0x80) {
            uint32_t cp;
            uint32_t adv = decode_utf8(src_ + cur_, len_ - cur_, cp);
            if (cp == 0x2028 || cp == 0x2029) break;
            cur_ += adv;
            continue;
        }
        cur_++;
    }

    return make(tok_regexp, start, cur_); // unterminated
}

Token Lexer2::rescan_as_regex(uint32_t slash_start) {
    cur_ = slash_start;
    flags_ = 0;
    return scan_regex();
}

// ─── Punctuation scanning ────────────────────────────────────────────────────

Token Lexer2::scan_punct() {
    uint32_t start = cur_;
    uint8_t c0 = peek(0), c1 = peek(1), c2 = peek(2), c3 = peek(3);

    switch (c0) {
    case '+':
        if (c1 == '+') { cur_ += 2; return make(tok_inc, start, cur_); }
        if (c1 == '=') { cur_ += 2; return make(tok_plus_assign, start, cur_); }
        cur_ += 1; return make(tok_plus, start, cur_);
    case '-':
        if (c1 == '-') { cur_ += 2; return make(tok_dec, start, cur_); }
        if (c1 == '=') { cur_ += 2; return make(tok_minus_assign, start, cur_); }
        cur_ += 1; return make(tok_minus, start, cur_);
    case '*':
        if (c1 == '*' && c2 == '=') { cur_ += 3; return make(tok_pow_assign, start, cur_); }
        if (c1 == '*')              { cur_ += 2; return make(tok_pow, start, cur_); }
        if (c1 == '=')              { cur_ += 2; return make(tok_star_assign, start, cur_); }
        cur_ += 1; return make(tok_star, start, cur_);
    case '/':
        if (c1 == '=') { cur_ += 2; return make(tok_slash_assign, start, cur_); }
        cur_ += 1; return make(tok_slash, start, cur_);
    case '%':
        if (c1 == '=') { cur_ += 2; return make(tok_pct_assign, start, cur_); }
        cur_ += 1; return make(tok_percent, start, cur_);
    case '=':
        if (c1 == '=' && c2 == '=') { cur_ += 3; return make(tok_seq, start, cur_); }
        if (c1 == '=')              { cur_ += 2; return make(tok_eq, start, cur_); }
        if (c1 == '>')              { cur_ += 2; return make(tok_arrow, start, cur_); }
        cur_ += 1; return make(tok_assign, start, cur_);
    case '!':
        if (c1 == '=' && c2 == '=') { cur_ += 3; return make(tok_sneq, start, cur_); }
        if (c1 == '=')              { cur_ += 2; return make(tok_neq, start, cur_); }
        cur_ += 1; return make(tok_bang, start, cur_);
    case '<':
        if (c1 == '<' && c2 == '=') { cur_ += 3; return make(tok_shl_assign, start, cur_); }
        if (c1 == '<')              { cur_ += 2; return make(tok_shl, start, cur_); }
        if (c1 == '=')              { cur_ += 2; return make(tok_lte, start, cur_); }
        cur_ += 1; return make(tok_lt, start, cur_);
    case '>':
        if (c1 == '>' && c2 == '>' && c3 == '=') { cur_ += 4; return make(tok_shr_assign, start, cur_); }
        if (c1 == '>' && c2 == '>')               { cur_ += 3; return make(tok_shr, start, cur_); }
        if (c1 == '>' && c2 == '=')               { cur_ += 3; return make(tok_sar_assign, start, cur_); }
        if (c1 == '>')                             { cur_ += 2; return make(tok_sar, start, cur_); }
        if (c1 == '=')                             { cur_ += 2; return make(tok_gte, start, cur_); }
        cur_ += 1; return make(tok_gt, start, cur_);
    case '&':
        if (c1 == '&' && c2 == '=') { cur_ += 3; return make(tok_land_assign, start, cur_); }
        if (c1 == '&')              { cur_ += 2; return make(tok_land, start, cur_); }
        if (c1 == '=')              { cur_ += 2; return make(tok_band_assign, start, cur_); }
        cur_ += 1; return make(tok_band, start, cur_);
    case '|':
        if (c1 == '|' && c2 == '=') { cur_ += 3; return make(tok_lor_assign, start, cur_); }
        if (c1 == '|')              { cur_ += 2; return make(tok_lor, start, cur_); }
        if (c1 == '=')              { cur_ += 2; return make(tok_bor_assign, start, cur_); }
        cur_ += 1; return make(tok_bor, start, cur_);
    case '^':
        if (c1 == '=') { cur_ += 2; return make(tok_bxor_assign, start, cur_); }
        cur_ += 1; return make(tok_bxor, start, cur_);
    case '~':
        cur_ += 1; return make(tok_bnot, start, cur_);
    case '?':
        if (c1 == '?' && c2 == '=') { cur_ += 3; return make(tok_nullish_assign, start, cur_); }
        if (c1 == '?' && c2 != '.') { cur_ += 2; return make(tok_nullish, start, cur_); }
        if (c1 == '.' && !std::isdigit(c2)) { cur_ += 2; return make(tok_opt_chain, start, cur_); }
        cur_ += 1; return make(tok_question, start, cur_);
    case '.':
        if (c1 == '.' && c2 == '.') { cur_ += 3; return make(tok_spread, start, cur_); }
        cur_ += 1; return make(tok_dot, start, cur_);
    case ',':
        cur_ += 1; return make(tok_comma, start, cur_);
    case ';':
        cur_ += 1; return make(tok_semi, start, cur_);
    case ':':
        cur_ += 1; return make(tok_colon, start, cur_);
    case '(':
        cur_ += 1; return make(tok_lparen, start, cur_);
    case ')':
        cur_ += 1; return make(tok_rparen, start, cur_);
    case '{':
        cur_ += 1; return make(tok_lbrace, start, cur_);
    case '}':
        cur_ += 1; return make(tok_rbrace, start, cur_);
    case '[':
        cur_ += 1; return make(tok_lbrack, start, cur_);
    case ']':
        cur_ += 1; return make(tok_rbrack, start, cur_);
    case '#':
        // lone # without ident — error, but return as-is
        cur_ += 1; return make(tok_private_ident, start, cur_);
    default:
        // Unknown character — skip and return EOF
        cur_ += 1;
        if (c0 >= 0x80) {
            // Try to skip full UTF-8 sequence
            uint32_t cp;
            cur_ = start + decode_utf8(src_ + start, len_ - start, cp);
        }
        return make(tok_eof, start, cur_);
    }
}

} // namespace qjsp
