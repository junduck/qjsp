#include "qjsp/parser2.hpp"

namespace qjsp {

// ─── Function parsing ────────────────────────────────────────────────────────

NodeIndex Parser::parse_function(bool is_expr, bool is_async) {
    uint32_t start = prev_end_;
    uint32_t flags = is_expr ? NF::IsExpr : 0;
    if (is_async) flags |= NF::Async;
    if (current_.tag == tok_star) {
        flags |= NF::Generator;
        advance();
    }
    if (current_.tag == tok_async) {
        flags |= NF::Async;
        advance();
    }

    NodeIndex id = NodeNull;
    if (is_ident_like()) {
        id = tree_.alloc(NK_BINDING_IDENT, cur_span());
        advance();
    } else if (!is_expr) {
        error("expected function name");
    }

    NodeIndex params = parse_formal_params();

    bool saved_yield = ctx_yield_;
    bool saved_await = ctx_await_;
    bool saved_return = ctx_return_;
    ctx_yield_ = flags & NF::Generator;
    ctx_await_ = flags & NF::Async;
    ctx_return_ = true;

    NodeIndex body = parse_function_body();

    ctx_yield_ = saved_yield;
    ctx_await_ = saved_await;
    ctx_return_ = saved_return;

    return tree_.alloc(NK_FUNCTION, span_from(start), id, params, body, flags);
}

NodeIndex Parser::parse_function_body() {
    uint32_t start = current_.start;
    expect(tok_lbrace);

    auto cp = static_cast<uint32_t>(scratch_stmts_.size());

    while (at(tok_string) && !current_.has_newline_before()) {
        uint32_t dir_start = current_.start;
        advance();
        NodeIndex dir = tree_.alloc(NK_DIRECTIVE, {dir_start, prev_end_});
        scratch_stmts_.push_back(dir);
        eat_semi();
        if (has_newline_before() || current_.tag != tok_string) break;
    }

    while (!at(tok_rbrace) && !at(tok_eof)) {
        NodeIndex s = parse_stmt();
        if (s != NodeNull) scratch_stmts_.push_back(s);
        else advance();
    }

    IndexRange body = flush_scratch(scratch_stmts_, cp);
    expect(tok_rbrace);
    return tree_.alloc(NK_FUNCTION_BODY, span_from(start), body.start, body.len);
}

NodeIndex Parser::parse_formal_params() {
    uint32_t start = current_.start;
    expect(tok_lparen);

    auto cp = static_cast<uint32_t>(scratch_a_.size());
    NodeIndex rest = NodeNull;

    while (!at(tok_rparen) && !at(tok_eof)) {
        if (at(tok_spread)) {
            advance();
            rest = parse_binding();
            if (eat(tok_assign)) {
                error("rest parameter may not have a default initializer");
                parse_expr(Prec::Assign);
            }
            if (at(tok_comma)) {
                error("rest parameter may not be followed by a comma");
                advance();
            }
            break;
        }
        uint32_t pstart = current_.start;
        NodeIndex name = parse_binding();
        NodeIndex init = NodeNull;
        if (eat(tok_assign)) {
            init = parse_expr(Prec::Assign);
        }
        NodeIndex param = tree_.alloc(NK_FORMAL_PARAM, span_from(pstart), name, init);
        scratch_a_.push_back(param);
        if (!at(tok_rparen)) expect(tok_comma);
    }

    IndexRange items = flush_scratch(scratch_a_, cp);
    expect(tok_rparen);
    return tree_.alloc(NK_FORMAL_PARAMS, span_from(start),
                       items.start, items.len, rest);
}

// ─── Class parsing ───────────────────────────────────────────────────────────

NodeIndex Parser::parse_class(bool is_expr) {
    uint32_t start = prev_end_;
    uint32_t flags = is_expr ? NF::IsExpr : 0;

    NodeIndex id = NodeNull;
    if (is_ident_like() && !at(tok_extends)) {
        id = tree_.alloc(NK_BINDING_IDENT, cur_span());
        advance();
    } else if (!is_expr) {
        error("expected class name");
    }

    NodeIndex super_class = NodeNull;
    if (eat(tok_extends)) {
        super_class = parse_expr(Prec::Assign);
    }

    NodeIndex body = parse_class_body();
    return tree_.alloc(NK_CLASS, span_from(start), id, super_class, body,
                       0, 0, flags);
}

NodeIndex Parser::parse_class_body() {
    uint32_t start = current_.start;
    expect(tok_lbrace);

    auto cp = static_cast<uint32_t>(scratch_a_.size());
    while (!at(tok_rbrace) && !at(tok_eof)) {
        if (eat(tok_semi)) continue;
        bool is_static = false;
        if (at(tok_static) && peek_class_element() != tok_lparen) {
            is_static = true;
            advance();
        }
        if (at(tok_lbrace)) {
            NodeIndex sb = parse_block();
            uint32_t flags = NF::Static;
            NodeIndex elem = tree_.alloc(NK_STATIC_BLOCK, tree_.span(sb),
                                         tree_.d(sb, 0), tree_.d(sb, 1), flags);
            scratch_a_.push_back(elem);
            continue;
        }
        NodeIndex elem = parse_class_element(is_static);
        if (elem != NodeNull) scratch_a_.push_back(elem);
        else if (!at(tok_semi) && !at(tok_rbrace) && !at(tok_eof)) advance();
    }

    IndexRange body = flush_scratch(scratch_a_, cp);
    expect(tok_rbrace);
    return tree_.alloc(NK_CLASS_BODY, span_from(start), body.start, body.len);
}

TokenTag Parser::peek_class_element() {
    uint32_t saved_cur = lexer_.cursor();
    Token next = lexer_.next_token();
    lexer_.set_cursor(saved_cur);
    return next.tag;
}

NodeIndex Parser::parse_class_element(bool is_static) {
    uint32_t start = current_.start;
    uint32_t flags = is_static ? NF::Static : 0;
    uint32_t method_kind = MethodInit;

    if (at(tok_get) || at(tok_set)) {
        if (is_ident_like()) {
            method_kind = at(tok_get) ? MethodGet : MethodSet;
            advance();
        }
    }

    if (at(tok_async)) {
        if (is_ident_like() || at(tok_lbrack) || at(tok_string) || tag_is_numeric(current_.tag)) {
            flags |= NF::Async;
            advance();
            if (at(tok_star)) {
                flags |= NF::Generator;
                advance();
            }
        }
    } else if (at(tok_star)) {
        flags |= NF::Generator;
        advance();
    }

    NodeIndex key = NodeNull;
    if (at(tok_lbrack)) {
        flags |= NF::Computed;
        advance();
        bool saved_in = ctx_in_;
        ctx_in_ = true;
        key = parse_expr();
        ctx_in_ = saved_in;
        expect(tok_rbrack);
    } else if (is_ident_like() || at(tok_string) || tag_is_numeric(current_.tag)) {
        Token key_tok = current_;
        advance();
        key = tree_.alloc(NK_IDENT_REF, {key_tok.start, prev_end_});
    } else {
        error("expected property name");
        return NodeNull;
    }

    if (at(tok_lparen)) {
        NodeIndex value = parse_function(true, flags & NF::Async);
        flags |= method_kind;
        return tree_.alloc(NK_METHOD_DEF, span_from(start), key, value, flags);
    }

    if (at(tok_assign)) {
        advance();
        NodeIndex value = parse_expr(Prec::Assign);
        eat(tok_semi);
        return tree_.alloc(NK_PROPERTY_DEF, span_from(start), key, value, flags);
    }

    eat(tok_semi);
    return tree_.alloc(NK_PROPERTY_DEF, span_from(start), key, NodeNull, flags);
}

// ─── Binding / pattern parsing ───────────────────────────────────────────────

NodeIndex Parser::parse_binding() {
    if (at(tok_lbrack)) return parse_binding_pattern();
    if (at(tok_lbrace)) return parse_binding_pattern();
    if (is_ident_like()) {
        Token tok = current_;
        advance();
        return tree_.alloc(NK_BINDING_IDENT, {tok.start, tok.end});
    }
    error("expected binding pattern");
    advance();
    return NodeNull;
}

NodeIndex Parser::parse_binding_pattern() {
    if (at(tok_lbrack)) {
        uint32_t start = current_.start;
        advance();
        auto cp = static_cast<uint32_t>(scratch_a_.size());
        NodeIndex rest = NodeNull;
        while (!at(tok_rbrack) && !at(tok_eof)) {
            if (at(tok_spread)) {
                advance();
                rest = parse_binding();
                break;
            }
            if (at(tok_comma)) {
                scratch_a_.push_back(NodeNull);
                advance();
                continue;
            }
            NodeIndex elem = parse_binding();
            if (eat(tok_assign)) {
                NodeIndex init = parse_expr(Prec::Assign);
                elem = tree_.alloc(NK_ASSIGNMENT_PAT,
                                   {tree_.span(elem).start, prev_end_},
                                   elem, init);
            }
            scratch_a_.push_back(elem);
            if (!at(tok_rbrack)) expect(tok_comma);
        }
        IndexRange elements = flush_scratch(scratch_a_, cp);
        expect(tok_rbrack);
        return tree_.alloc(NK_ARRAY_PAT, span_from(start),
                           elements.start, elements.len, rest);
    }

    if (at(tok_lbrace)) {
        uint32_t start = current_.start;
        advance();
        auto cp = static_cast<uint32_t>(scratch_a_.size());
        NodeIndex rest = NodeNull;
        while (!at(tok_rbrace) && !at(tok_eof)) {
            if (at(tok_spread)) {
                advance();
                rest = parse_binding();
                break;
            }
            uint32_t prop_start = current_.start;
            uint32_t flags = 0;
            NodeIndex key = NodeNull;
            NodeIndex value = NodeNull;

            if (at(tok_lbrack)) {
                flags |= NF::Computed;
                advance();
                bool saved_in = ctx_in_;
                ctx_in_ = true;
                key = parse_expr();
                ctx_in_ = saved_in;
                expect(tok_rbrack);
                expect(tok_colon);
                value = parse_binding();
            } else if (is_ident_like() || at(tok_string) || tag_is_numeric(current_.tag)) {
                Token id_tok = current_;
                bool is_shorthand = is_ident_like() && !at(tok_string) && !tag_is_numeric(current_.tag);
                advance();
                key = tree_.alloc(NK_IDENT_REF, {id_tok.start, prev_end_});
                if (eat(tok_colon)) {
                    value = parse_binding();
                } else if (is_shorthand) {
                    flags |= NF::Shorthand;
                    value = tree_.alloc(NK_BINDING_IDENT, {id_tok.start, id_tok.end});
                } else {
                    error("expected property name");
                    continue;
                }
            } else {
                error("expected property name");
                continue;
            }

            if (eat(tok_assign)) {
                NodeIndex init = parse_expr(Prec::Assign);
                value = tree_.alloc(NK_ASSIGNMENT_PAT,
                                    {tree_.span(value).start, prev_end_},
                                    value, init);
            }

            NodeIndex prop = tree_.alloc(NK_BINDING_PROP, span_from(prop_start),
                                         key, value, flags);
            scratch_a_.push_back(prop);
            if (!at(tok_rbrace)) expect(tok_comma);
        }
        IndexRange props = flush_scratch(scratch_a_, cp);
        expect(tok_rbrace);
        return tree_.alloc(NK_OBJECT_PAT, span_from(start),
                           props.start, props.len, rest);
    }

    error("expected binding pattern");
    return NodeNull;
}

} // namespace qjsp
