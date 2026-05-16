#include "qjsp/parser2.hpp"

namespace qjsp {

NodeIndex Parser::parse_primary() {
    auto tag = current_.tag;
    uint32_t start = current_.start;

    if (tag_is_numeric(tag)) {
        advance();
        return tree_.alloc(NK_NUMERIC_LIT, {start, prev_end_});
    }

    if (tag == tok_bigint) {
        advance();
        return tree_.alloc(NK_BIGINT_LIT, {start, prev_end_});
    }

    switch (tag) {
    case tok_string: {
        advance();
        return tree_.alloc(NK_STRING_LIT, {start, prev_end_});
    }
    case tok_true:
        advance();
        return tree_.alloc(NK_BOOL_LIT, {start, prev_end_}, 1);
    case tok_false:
        advance();
        return tree_.alloc(NK_BOOL_LIT, {start, prev_end_}, 0);
    case tok_null:
        advance();
        return tree_.alloc(NK_NULL_LIT, {start, prev_end_});
    case tok_this:
        advance();
        return tree_.alloc(NK_THIS_EXPR, {start, prev_end_});
    case tok_super:
        advance();
        if (current_.tag != tok_lparen && current_.tag != tok_dot &&
            current_.tag != tok_lbrack) {
            error("'super' must be followed by a call or property access");
        }
        return tree_.alloc(NK_SUPER, {start, prev_end_});
    case tok_private_ident:
        advance();
        if (current_.tag != tok_dot && current_.tag != tok_in) {
            error("private names are only valid in property accesses or 'in' expressions");
        }
        return tree_.alloc(NK_PRIVATE_IDENT, {start, prev_end_});

    case tok_slash:
    case tok_slash_assign: {
        Token t = lexer_.rescan_as_regex(current_.start);
        current_ = t;
        advance();
        return tree_.alloc(NK_REGEXP_LIT, {start, prev_end_});
    }

    case tok_template_full: {
        advance();
        NodeIndex quasi = tree_.alloc(NK_TEMPLATE_ELEM, {start, prev_end_}, NF::Tail);
        IndexRange quasis = tree_.add_extras(&quasi, 1);
        IndexRange empty2 = {0, 0};
        return tree_.alloc(NK_TEMPLATE_LIT, span_from(start),
                           quasis.start, quasis.len, empty2.start, empty2.len);
    }

    case tok_template_head: {
        Span opener = {current_.start, current_.end};
        advance();
        return parse_template_lit(opener);
    }

    case tok_lbrack:
        return parse_array_expr();

    case tok_lbrace:
        return parse_object_expr();

    case tok_function:
        advance();
        return parse_function(true);

    case tok_class:
        advance();
        return parse_class(true);

    case tok_import:
        advance();
        if (at(tok_dot)) {
            uint32_t import_end = prev_end_;
            advance();
            Token prop = expect(tok_ident);
            if (!source_eq(prop.start, prop.end, "meta")) {
                error("the only valid meta property for 'import' is 'import.meta'");
            }
            return tree_.alloc(NK_META_PROPERTY, {start, prev_end_},
                               start, import_end, prop.start, prop.end);
        }
        expect(tok_lparen);
        {
            NodeIndex arg = parse_expr(Prec::Assign);
            expect(tok_rparen);
            return tree_.alloc(NK_IMPORT_EXPR, {start, prev_end_}, arg);
        }

    default:
        if (is_ident_like()) {
            advance();
            return tree_.alloc(NK_IDENT_REF, {start, prev_end_});
        }
        error("unexpected token");
        advance();
        return NodeNull;
    }
}

NodeIndex Parser::parse_array_expr() {
    uint32_t start = current_.start;
    expect(tok_lbrack);
    bool saved_cover = in_cover_;
    in_cover_ = true;
    auto cp = static_cast<uint32_t>(scratch_a_.size());
    while (!at(tok_rbrack) && !at(tok_eof)) {
        if (eat(tok_comma)) {
            scratch_a_.push_back(NodeNull);
            continue;
        }
        if (at(tok_spread)) {
            advance();
            NodeIndex elem = parse_expr(Prec::Assign);
            NodeIndex sp = tree_.alloc(NK_SPREAD, span_from(current_.start), elem);
            scratch_a_.push_back(sp);
        } else {
            scratch_a_.push_back(parse_expr(Prec::Assign));
        }
        if (!at(tok_rbrack)) expect(tok_comma);
    }
    IndexRange elems = flush_scratch(scratch_a_, cp);
    expect(tok_rbrack);
    in_cover_ = saved_cover;
    return tree_.alloc(NK_ARRAY_EXPR, span_from(start), elems.start, elems.len);
}

NodeIndex Parser::parse_object_expr() {
    uint32_t start = current_.start;
    expect(tok_lbrace);
    bool saved_cover = in_cover_;
    in_cover_ = true;
    auto cp = static_cast<uint32_t>(scratch_a_.size());
    while (!at(tok_rbrace) && !at(tok_eof)) {
        uint32_t prop_start = current_.start;
        uint32_t flags = 0;
        NodeIndex key = NodeNull;
        uint32_t method_kind = MethodInit;

        if (at(tok_spread)) {
            advance();
            NodeIndex elem = parse_expr(Prec::Assign);
            NodeIndex sp = tree_.alloc(NK_SPREAD, span_from(prop_start), elem);
            scratch_a_.push_back(sp);
            if (!at(tok_rbrace)) eat(tok_comma);
            continue;
        }

        bool is_generator = false;
        bool is_async = false;
        if (at(tok_star)) {
            is_generator = true;
            advance();
        } else if (at(tok_async)) {
            Token async_tok = current_;
            uint32_t saved_cursor = lexer_.cursor();
            advance();
            if (at(tok_star)) {
                is_async = true;
                is_generator = true;
                advance();
            } else if (at(tok_lparen) || (is_ident_like() && !current_.has_newline_before())) {
                is_async = true;
            } else {
                lexer_.set_cursor(saved_cursor);
                current_ = async_tok;
            }
            if (is_async) method_kind = MethodAsync;
        } else if (at(tok_get) || at(tok_set)) {
            Token gs_tok = current_;
            advance();
            if (at(tok_colon) || at(tok_comma) || at(tok_rbrace) || at(tok_assign)) {
                key = tree_.alloc(NK_IDENT_REF, {gs_tok.start, gs_tok.end});
            } else if (is_ident_like() || at(tok_string) || at(tok_lbrack) || tag_is_numeric(current_.tag)) {
                method_kind = (gs_tok.tag == tok_get) ? MethodGet : MethodSet;
            } else {
                key = tree_.alloc(NK_IDENT_REF, {gs_tok.start, gs_tok.end});
                flags |= NF::Shorthand;
                NodeIndex value = tree_.alloc(NK_IDENT_REF, {gs_tok.start, gs_tok.end});
                NodeIndex prop = tree_.alloc(NK_OBJECT_PROP, span_from(prop_start),
                                             key, value, flags);
                scratch_a_.push_back(prop);
                if (at(tok_comma)) { advance(); continue; }
                if (!at(tok_rbrace)) expect(tok_comma);
                continue;
            }
        }

        if (key == NodeNull) {
            if (at(tok_lbrack)) {
                flags |= NF::Computed;
                advance();
                key = parse_expr();
                expect(tok_rbrack);
            } else if (is_ident_like() || at(tok_string) || tag_is_numeric(current_.tag)) {
                Token key_tok = current_;
                advance();
                key = tree_.alloc(NK_IDENT_REF, {key_tok.start, prev_end_});
            } else {
                error("expected property name");
                advance();
                continue;
            }
        }

        if (at(tok_lparen)) {
            uint32_t fn_flags = NF::IsExpr;
            if (is_generator) fn_flags |= NF::Generator;
            if (is_async) fn_flags |= NF::Async;
            NodeIndex params = parse_formal_params();
            bool saved_yield = ctx_yield_;
            bool saved_await = ctx_await_;
            bool saved_return = ctx_return_;
            ctx_yield_ = is_generator;
            ctx_await_ = is_async;
            ctx_return_ = true;
            NodeIndex body = parse_function_body();
            ctx_yield_ = saved_yield;
            ctx_await_ = saved_await;
            ctx_return_ = saved_return;
            NodeIndex fn = tree_.alloc(NK_FUNCTION, span_from(prop_start),
                                       NodeNull, params, body, fn_flags);
            NodeIndex method = tree_.alloc(NK_METHOD_DEF, span_from(prop_start),
                                           key, fn, method_kind | (flags & NF::Computed ? 1u << 8 : 0));
            scratch_a_.push_back(method);
            if (!at(tok_rbrace)) expect(tok_comma);
            continue;
        }

        if (method_kind != MethodInit || is_generator || is_async) {
            error("expected '(' for method definition");
            continue;
        }

        if (at(tok_colon)) {
            advance();
        } else if (flags & NF::Computed) {
            error("expected ':' or '('");
            continue;
        } else if (at(tok_assign)) {
            cover_has_init_name_ = true;
            advance();
            NodeIndex init = parse_expr(Prec::Assign);
            NodeIndex id_ref = tree_.alloc(NK_IDENT_REF, tree_.span(key));
            NodeIndex value = tree_.alloc(NK_ASSIGNMENT_EXPR, span_from(prop_start),
                                          id_ref, init, AsgnAssign);
            flags |= NF::Shorthand;
            NodeIndex prop = tree_.alloc(NK_OBJECT_PROP, span_from(prop_start),
                                          key, value, flags);
            scratch_a_.push_back(prop);
            if (at(tok_comma)) { advance(); continue; }
            if (!at(tok_rbrace)) expect(tok_comma);
            continue;
        } else {
            flags |= NF::Shorthand;
            NodeIndex value = tree_.alloc(NK_IDENT_REF, tree_.span(key));
            NodeIndex prop = tree_.alloc(NK_OBJECT_PROP, span_from(prop_start),
                                          key, value, flags);
            scratch_a_.push_back(prop);
            if (at(tok_comma)) { advance(); continue; }
            if (!at(tok_rbrace)) expect(tok_comma);
            continue;
        }

        NodeIndex value;
        if (at(tok_function)) {
            advance();
            value = parse_function(true);
        } else {
            value = parse_expr(Prec::Assign);
        }

        NodeIndex prop = tree_.alloc(NK_OBJECT_PROP, span_from(prop_start),
                                     key, value, flags);
        scratch_a_.push_back(prop);
        if (!at(tok_rbrace)) expect(tok_comma);
    }
    IndexRange props = flush_scratch(scratch_a_, cp);
    expect(tok_rbrace);
    NodeIndex obj = tree_.alloc(NK_OBJECT_EXPR, span_from(start), props.start, props.len);
    if (!saved_cover && cover_has_init_name_) {
        if (!at(tok_assign) && !at(tok_in) && !at(tok_of)) {
            validate_cover_init(obj);
        }
        cover_has_init_name_ = false;
    }
    in_cover_ = saved_cover;
    return obj;
}

NodeIndex Parser::parse_paren_or_arrow() {
    uint32_t start = current_.start;
    advance();

    if (at(tok_rparen)) {
        advance();
        expect(tok_arrow);
        NodeIndex params = tree_.alloc(NK_FORMAL_PARAMS, span_from(start),
                                       0, 0, NodeNull);
        bool saved_yield = ctx_yield_;
        ctx_yield_ = false;
        NodeIndex body = parse_arrow_body();
        ctx_yield_ = saved_yield;
        uint32_t flags = NF::IsExpr;
        if (tree_.kind(body) != NK_BLOCK_STMT) flags |= NF::ExprBody;
        return tree_.alloc(NK_ARROW_FUNCTION, span_from(start), params, body, flags);
    }

    auto cp = static_cast<uint32_t>(scratch_cover_.size());
    bool saved_cover = in_cover_;
    in_cover_ = true;

    NodeIndex first = NodeNull;
    if (at(tok_spread)) {
        uint32_t spread_start = current_.start;
        advance();
        NodeIndex arg = parse_expr(Prec::Assign);
        first = tree_.alloc(NK_SPREAD, {spread_start, prev_end_}, arg);
    } else {
        first = parse_expr(Prec::Assign);
    }
    scratch_cover_.push_back(first);

    bool has_trailing_comma = false;
    while (eat(tok_comma)) {
        if (at(tok_rparen)) { has_trailing_comma = true; break; }
        if (at(tok_spread)) {
            uint32_t spread_start = current_.start;
            advance();
            NodeIndex arg = parse_expr(Prec::Assign);
            NodeIndex sp = tree_.alloc(NK_SPREAD, {spread_start, prev_end_}, arg);
            scratch_cover_.push_back(sp);
        } else {
            scratch_cover_.push_back(parse_expr(Prec::Assign));
        }
    }
    expect(tok_rparen);
    in_cover_ = saved_cover;

    if (at(tok_arrow) && !has_newline_before()) {
        advance();
        cover_has_init_name_ = false;
        NodeIndex params = build_arrow_params(
            scratch_cover_.size() - cp == 1
                ? first
                : NodeNull);
        if (scratch_cover_.size() - cp > 1) {
            auto pcp = static_cast<uint32_t>(scratch_a_.size());
            for (uint32_t i = cp; i < scratch_cover_.size(); i++) {
                NodeIndex el = scratch_cover_[i];
                if (el != NodeNull) scratch_a_.push_back(expr_to_binding(el));
            }
            IndexRange items = flush_scratch(scratch_a_, pcp);
            params = tree_.alloc(NK_FORMAL_PARAMS, span_from(start),
                                 items.start, items.len, NodeNull);
        } else {
            params = build_arrow_params(first);
        }
        scratch_cover_.resize(cp);
        bool saved_yield = ctx_yield_;
        ctx_yield_ = false;
        NodeIndex body = parse_arrow_body();
        ctx_yield_ = saved_yield;
        uint32_t flags = NF::IsExpr;
        if (tree_.kind(body) != NK_BLOCK_STMT) flags |= NF::ExprBody;
        return tree_.alloc(NK_ARROW_FUNCTION, span_from(start), params, body, flags);
    }

    NodeIndex result;
    if (scratch_cover_.size() - cp == 1) {
        result = first;
    } else {
        IndexRange exprs;
        exprs.start = static_cast<uint32_t>(
            tree_.extras.size());
        exprs.len = static_cast<uint32_t>(scratch_cover_.size() - cp);
        for (uint32_t i = cp; i < scratch_cover_.size(); i++) {
            tree_.extras.push_back(scratch_cover_[i]);
        }
        result = tree_.alloc(NK_SEQUENCE_EXPR, span_from(start),
                             exprs.start, exprs.len);
    }

    for (uint32_t i = cp; i < scratch_cover_.size(); i++) {
        NodeIndex el = scratch_cover_[i];
        if (el == NodeNull) continue;
        if (tree_.kind(el) == NK_SPREAD) {
            error_at(tree_.span(el),
                "rest element is not allowed in parenthesized expression");
        }
        validate_cover_init(el);
    }

    if (has_trailing_comma) {
        error("trailing comma is not allowed in parenthesized expression");
    }

    scratch_cover_.resize(cp);
    return tree_.alloc(NK_PAREN_EXPR, span_from(start), result);
}

NodeIndex Parser::expr_to_binding(NodeIndex expr) {
    switch (tree_.kind(expr)) {
    case NK_IDENT_REF:
        tree_.nodes[expr].kind = NK_BINDING_IDENT;
        return expr;
    case NK_PAREN_EXPR:
        return expr_to_binding(tree_.d(expr, 0));
    case NK_ASSIGNMENT_EXPR: {
        NodeIndex left = tree_.d(expr, 0);
        NodeIndex right = tree_.d(expr, 1);
        return tree_.alloc(NK_ASSIGNMENT_PAT, tree_.span(expr),
                           expr_to_binding(left), right);
    }
    case NK_ARRAY_EXPR:
        tree_.nodes[expr].kind = NK_ARRAY_PAT;
        return expr;
    case NK_OBJECT_EXPR:
        tree_.nodes[expr].kind = NK_OBJECT_PAT;
        return expr;
    case NK_SPREAD:
        tree_.nodes[expr].kind = NK_BINDING_REST;
        return expr;
    default:
        return expr;
    }
}

NodeIndex Parser::build_arrow_params(NodeIndex single) {
    if (single != NodeNull) {
        NodeIndex binding = expr_to_binding(single);
        IndexRange items = tree_.add_extras(&binding, 1);
        return tree_.alloc(NK_FORMAL_PARAMS, tree_.span(single),
                           items.start, items.len, NodeNull);
    }
    return tree_.alloc(NK_FORMAL_PARAMS, {0, 0}, 0, 0, NodeNull);
}

NodeIndex Parser::parse_template_lit(Span opener) {
    uint32_t start = opener.start;
    auto cp_q = static_cast<uint32_t>(scratch_a_.size());
    auto cp_e = static_cast<uint32_t>(scratch_b_.size());

    NodeIndex quasi = tree_.alloc(NK_TEMPLATE_ELEM, opener, 0);
    scratch_a_.push_back(quasi);

    scratch_b_.push_back(parse_expr());

    while (true) {
        if (at(tok_template_mid)) {
            Token mid_tok = current_;
            advance();
            quasi = tree_.alloc(NK_TEMPLATE_ELEM, {mid_tok.start, mid_tok.end}, 0);
            scratch_a_.push_back(quasi);
            scratch_b_.push_back(parse_expr());
        } else if (at(tok_template_tail)) {
            Token tail_tok = current_;
            advance();
            quasi = tree_.alloc(NK_TEMPLATE_ELEM, {tail_tok.start, tail_tok.end}, NF::Tail);
            scratch_a_.push_back(quasi);
            break;
        } else {
            error("unterminated template literal");
            break;
        }
    }

    IndexRange quasis = flush_scratch(scratch_a_, cp_q);
    IndexRange exprs = flush_scratch(scratch_b_, cp_e);
    return tree_.alloc(NK_TEMPLATE_LIT, span_from(start),
                       quasis.start, quasis.len, exprs.start, exprs.len);
}

NodeIndex Parser::parse_simple_arrow(NodeIndex param) {
    uint32_t start = tree_.span(param).start;
    advance();

    IndexRange items = tree_.add_extras(&param, 1);
    NodeIndex params = tree_.alloc(NK_FORMAL_PARAMS, tree_.span(param),
                                   items.start, items.len, NodeNull);

    bool saved_yield = ctx_yield_;
    ctx_yield_ = false;
    NodeIndex body = parse_arrow_body();
    ctx_yield_ = saved_yield;
    uint32_t flags = NF::IsExpr | NF::ExprBody;
    if (tree_.kind(body) == NK_BLOCK_STMT) flags = NF::IsExpr;
    return tree_.alloc(NK_ARROW_FUNCTION, span_from(start),
                       params, body, flags);
}

NodeIndex Parser::parse_arrow_body() {
    if (at(tok_lbrace)) {
        return parse_function_body();
    }
    NodeIndex expr = parse_expr(Prec::Assign);
    return expr;
}

} // namespace qjsp
