#pragma once

#include "qjsp/token2.hpp"
#include <cstdint>
#include <vector>

namespace qjsp {

struct Engine;

struct Comment {
    uint32_t start;
    uint32_t end;
    bool is_block;
};

struct LexerHashbang {
    uint32_t start = 0;
    uint32_t end = 0;
};

struct Lexer2 {
    const uint8_t *src_ = nullptr;
    uint32_t       len_ = 0;
    uint32_t       cur_ = 0;
    uint8_t        flags_ = 0;
    bool           is_module_ = true;
    std::vector<Comment> comments;
    LexerHashbang hashbang;

    void init(const uint8_t *source, uint32_t source_len, bool is_module = true);

    Token next_token();

    Token rescan_as_regex(uint32_t slash_start);
    Token rescan_template(uint32_t rbrace_start);

    uint32_t cursor() const { return cur_; }
    void set_cursor(uint32_t pos) { cur_ = pos; flags_ = 0; }

    const uint8_t *source() const { return src_; }

private:
    uint8_t peek(uint32_t offset) const;
    Token make(TokenTag tag, uint32_t start, uint32_t end) const;

    void skip_ws_and_comments();
    void skip_hashbang();

    Token scan_ident_or_keyword();
    Token scan_number();
    Token scan_string();
    Token scan_template_part();
    Token scan_regex();
    Token scan_punct();
    void skip_escape();

    TokenTag classify_keyword(const uint8_t *s, uint32_t len) const;
    static uint32_t decode_utf8(const uint8_t *p, uint32_t max_len, uint32_t &cp);
};

} // namespace qjsp
