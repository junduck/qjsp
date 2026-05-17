#pragma once

#include "qjsp/ast.hpp"
#include "qjsp/lexer2.hpp"
#include <vector>

namespace qjsp {

struct ParseCtx {
    static constexpr uint8_t In = 1 << 0;
    static constexpr uint8_t Yield = 1 << 1;
    static constexpr uint8_t Await = 1 << 2;
    static constexpr uint8_t Return = 1 << 3;
    static constexpr uint8_t SingleStmt = 1 << 4;
    static constexpr uint8_t DirPrologue = 1 << 5;

    uint8_t flags = In;

    bool has(uint8_t f) const { return (flags & f) != 0; }
    void set(uint8_t f) { flags |= f; }
    void clear(uint8_t f) { flags &= ~f; }
};

class Parser {
    friend struct CtxGuard;
    struct CtxGuard {
        Parser &p;
        uint8_t saved;
        CtxGuard(Parser &p, uint8_t new_flags) : p(p), saved(p.ctx_.flags) {
            p.ctx_.flags = new_flags;
        }
        ~CtxGuard() { p.ctx_.flags = saved; }
        CtxGuard(const CtxGuard &) = delete;
        CtxGuard &operator=(const CtxGuard &) = delete;
    };

public:
    void init(const uint8_t *source, uint32_t len);
    NodeIndex parse();

    AstTree &tree() { return tree_; }
    const AstTree &tree() const { return tree_; }

private:
    Lexer2 lexer_;
    AstTree tree_;
    Token current_;
    uint32_t prev_end_ = 0;

    ParseCtx ctx_;
    bool cover_has_init_name_ = false;
    bool in_cover_ = false;

    std::vector<uint32_t> scratch_stmts_;
    std::vector<uint32_t> scratch_cover_;
    std::vector<uint32_t> scratch_a_;
    std::vector<uint32_t> scratch_b_;

    Token advance();
    Token expect(TokenTag tag);
    bool eat(TokenTag tag);
    bool eat_semi();
    bool at(TokenTag tag) const;
    bool has_newline_before() const;
    Token peek_ahead();

    void error(const char *msg);
    void error_at(Token tok, const char *msg);
    void error_at(Span sp, const char *msg);

    IndexRange flush_scratch(std::vector<uint32_t> &s, uint32_t cp);

    // ─── Expression parsing ────────────────────────────────────────────────
    NodeIndex parse_expr(uint8_t min_prec = Prec::Comma);
    NodeIndex parse_prefix();
    NodeIndex parse_infix(uint8_t prec, NodeIndex left);
    uint8_t infix_prec() const;
    uint8_t max_left_prec(NodeIndex n) const;

    // ─── Primary expressions ───────────────────────────────────────────────
    NodeIndex parse_primary();
    NodeIndex parse_array_expr();
    NodeIndex parse_object_expr();
    NodeIndex parse_paren_or_arrow();
    NodeIndex parse_template_lit(Span opener);
    NodeIndex parse_simple_arrow(NodeIndex param);
    NodeIndex parse_arrow_body();
    NodeIndex expr_to_binding(NodeIndex expr);
    NodeIndex build_arrow_params(NodeIndex expr);

    // ─── Statement parsing ─────────────────────────────────────────────────
    NodeIndex parse_stmt();
    NodeIndex parse_stmt_list_body();
    NodeIndex parse_block();
    NodeIndex parse_expr_or_label_or_directive();
    NodeIndex parse_if_stmt();
    NodeIndex parse_switch_stmt();
    IndexRange parse_switch_cases();
    NodeIndex parse_for_stmt();
    NodeIndex parse_for_rest(NodeIndex init_or_expr, bool init_is_for, bool is_await);
    NodeIndex parse_while_stmt();
    NodeIndex parse_do_while_stmt();
    NodeIndex parse_with_stmt();
    NodeIndex parse_try_stmt();
    NodeIndex parse_catch();
    NodeIndex parse_break_stmt();
    NodeIndex parse_continue_stmt();
    NodeIndex parse_return_stmt();
    NodeIndex parse_throw_stmt();
    NodeIndex parse_debugger_stmt();
    NodeIndex parse_empty_stmt();
    NodeIndex parse_var_decl(bool eat_semi_ = true);
    NodeIndex parse_var_decl_with_kind(uint32_t kind, uint32_t start, bool eat_semi_ = true);
    NodeIndex parse_var_decl_body(uint32_t kind, uint32_t start, bool eat_semi_);
    bool is_using_decl();

    // ─── Functions / classes ───────────────────────────────────────────────
    NodeIndex parse_function(bool is_expr, bool is_async = false);
    NodeIndex parse_function_with_flags(uint32_t pre_flags);
    NodeIndex parse_function_body();
    NodeIndex parse_formal_params();
    NodeIndex parse_class(bool is_expr);
    NodeIndex parse_class_body();
    TokenTag peek_class_element();
    NodeIndex parse_class_element(bool is_static);

    // ─── Modules (import/export) ───────────────────────────────────────────
    NodeIndex parse_import_decl();
    IndexRange parse_named_imports();
    NodeIndex parse_import_spec();
    NodeIndex parse_export_decl();
    NodeIndex parse_export_default();
    NodeIndex parse_export_named();
    NodeIndex parse_export_all();
    NodeIndex parse_export_spec();

    void validate_for_init_declarators(NodeIndex decl);
    void validate_const_init(NodeIndex decl);
    void validate_using_init(NodeIndex decl);
    void validate_cover_init(NodeIndex expr);

    // ─── Destructuring / patterns ──────────────────────────────────────────
    NodeIndex parse_binding();
    NodeIndex parse_binding_pattern();

    // ─── Operator mapping ──────────────────────────────────────────────────
    static BinOp token_binop(TokenTag t);
    static LogOp token_logop(TokenTag t);
    static AsgnOp token_asgnop(TokenTag t);
    static UnOp token_unop(TokenTag t);
    static UpdOp token_updop(TokenTag t);

    // ─── Helpers ───────────────────────────────────────────────────────────
    Span span_from(uint32_t start) const;
    Span node_span(NodeIndex n) const;
    Span cur_span() const;
    bool is_ident_like() const;
    NodeIndex wrap_chain(NodeIndex left, NodeIndex inner, Span sp);
    NodeKind kind(NodeIndex n) const { return tree_.kind(n); }
    bool source_eq(uint32_t start, uint32_t end, const char *lit) const;
    bool is_simple_assign_target(NodeIndex n) const;
};

} // namespace qjsp
