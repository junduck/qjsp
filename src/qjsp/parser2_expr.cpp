#include "qjsp/parser2.hpp"

namespace qjsp {

// ─── Helpers ─────────────────────────────────────────────────────────────────

Token Parser::advance() {
    Token prev = current_;
    prev_end_ = current_.end;
    current_ = lexer_.next_token();
    return prev;
}

Token Parser::expect(TokenTag tag) {
    if (current_.tag != tag) {
        error_at(current_, "unexpected token");
        return advance();
    }
    return advance();
}

bool Parser::eat(TokenTag tag) {
    if (current_.tag != tag) return false;
    advance();
    return true;
}

bool Parser::eat_semi() {
    if (current_.tag == tok_semi) {
        advance();
        return true;
    }
    if (current_.tag == tok_eof || current_.tag == tok_rbrace) return true;
    if (has_newline_before()) return true;
    error("expected semicolon");
    return false;
}

bool Parser::at(TokenTag tag) const { return current_.tag == tag; }

bool Parser::has_newline_before() const { return current_.has_newline_before(); }

void Parser::error(const char *msg) { error_at(current_, msg); }

void Parser::error_at(Token tok, const char *msg) {
    tree_.error({tok.start, tok.end}, msg);
}

void Parser::error_at(Span sp, const char *msg) {
    tree_.error(sp, msg);
}

Span Parser::span_from(uint32_t start) const {
    return {start, prev_end_};
}

Span Parser::node_span(NodeIndex n) const { return tree_.span(n); }

Span Parser::cur_span() const { return {current_.start, current_.end}; }

bool Parser::is_ident_like() const { return tag_is_ident_like(current_.tag); }

NodeIndex Parser::wrap_chain(NodeIndex left, NodeIndex inner, Span sp) {
    if (kind(left) == NK_CHAIN_EXPR) {
        return tree_.alloc(NK_CHAIN_EXPR, sp, left, inner);
    }
    return inner;
}
IndexRange Parser::flush_scratch(std::vector<uint32_t> &s, uint32_t cp) {
    auto count = static_cast<uint32_t>(s.size() - cp);
    if (count == 0) return {0, 0};
    IndexRange r = tree_.add_extras(s.data() + cp, count);
    s.resize(cp);
    return r;
}

bool Parser::source_eq(uint32_t start, uint32_t end, const char *lit) const {
    uint32_t len = end - start;
    for (uint32_t i = 0; i < len; i++) {
        if (lit[i] == '\0' || lexer_.source()[start + i] != (uint8_t)lit[i]) return false;
    }
    return lit[len] == '\0';
}

bool Parser::is_simple_assign_target(NodeIndex n) const {
    NodeKind k = kind(n);
    if (k == NK_IDENT_REF || k == NK_BINDING_IDENT) return true;
    if (k == NK_MEMBER_EXPR) return !(tree_.d(n, 2) & NF::Optional);
    if (k == NK_PAREN_EXPR) return is_simple_assign_target(tree_.d(n, 0));
    return false;
}

void Parser::validate_cover_init(NodeIndex n) {
    NodeKind k = kind(n);
    switch (k) {
    case NK_SEQUENCE_EXPR: {
        IndexRange exprs = tree_.range(n, 0);
        for (uint32_t i = 0; i < exprs.len; i++)
            validate_cover_init(tree_.extra(exprs)[i]);
        break;
    }
    case NK_PAREN_EXPR:
        validate_cover_init(tree_.d(n, 0));
        break;
    case NK_ARRAY_EXPR: {
        IndexRange elems = tree_.range(n, 0);
        for (uint32_t i = 0; i < elems.len; i++) {
            NodeIndex el = tree_.extra(elems)[i];
            if (el != NodeNull) validate_cover_init(el);
        }
        break;
    }
    case NK_OBJECT_EXPR: {
        IndexRange props = tree_.range(n, 0);
        for (uint32_t i = 0; i < props.len; i++) {
            NodeIndex prop = tree_.extra(props)[i];
            if (tree_.kind(prop) == NK_OBJECT_PROP) validate_cover_init(prop);
            else if (tree_.kind(prop) == NK_SPREAD) validate_cover_init(tree_.d(prop, 0));
        }
        break;
    }
    case NK_OBJECT_PROP: {
        uint32_t flags = tree_.d(n, 2);
        if (flags & NF::Shorthand) {
            NodeIndex val = tree_.d(n, 1);
            if (tree_.kind(val) == NK_ASSIGNMENT_EXPR &&
                tree_.d(val, 2) == AsgnAssign) {
                error_at(tree_.span(n),
                    "shorthand property cannot have a default value in object expression");
                break;
            }
        }
        validate_cover_init(tree_.d(n, 1));
        break;
    }
    case NK_BINARY_EXPR:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        break;
    case NK_LOGICAL_EXPR:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        break;
    case NK_ASSIGNMENT_EXPR:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        break;
    case NK_CONDITIONAL_EXPR:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        validate_cover_init(tree_.d(n, 2));
        break;
    case NK_UNARY_EXPR:
    case NK_UPDATE_EXPR:
    case NK_AWAIT_EXPR:
    case NK_YIELD_EXPR:
    case NK_SPREAD: {
        NodeIndex arg = tree_.d(n, 0);
        if (arg != NodeNull) validate_cover_init(arg);
        break;
    }
    case NK_CALL_EXPR: {
        validate_cover_init(tree_.d(n, 0));
        IndexRange args = tree_.range(n, 1);
        for (uint32_t i = 0; i < args.len; i++)
            validate_cover_init(tree_.extra(args)[i]);
        break;
    }
    case NK_NEW_EXPR: {
        validate_cover_init(tree_.d(n, 0));
        IndexRange args = tree_.range(n, 1);
        for (uint32_t i = 0; i < args.len; i++)
            validate_cover_init(tree_.extra(args)[i]);
        break;
    }
    case NK_CHAIN_EXPR:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        break;
    case NK_MEMBER_EXPR:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        break;
    case NK_TAGGED_TEMPLATE:
        validate_cover_init(tree_.d(n, 0));
        break;
    case NK_METHOD_DEF:
    case NK_PROPERTY_DEF:
        validate_cover_init(tree_.d(n, 0));
        validate_cover_init(tree_.d(n, 1));
        break;
    default:
        break;
    }
}

// ─── Operator mapping ────────────────────────────────────────────────────────

BinOp Parser::token_binop(TokenTag t) {
    switch (t) {
    case tok_eq:   return BinEq;
    case tok_neq:  return BinNeq;
    case tok_seq:  return BinSeq;
    case tok_sneq: return BinSneq;
    case tok_lt:   return BinLt;
    case tok_gt:   return BinGt;
    case tok_lte:  return BinLte;
    case tok_gte:  return BinGte;
    case tok_instanceof: return BinInstanceof;
    case tok_in:   return BinIn;
    case tok_shl:  return BinShl;
    case tok_sar:  return BinSar;
    case tok_shr:  return BinShr;
    case tok_plus: return BinAdd;
    case tok_minus:return BinSub;
    case tok_star: return BinMul;
    case tok_slash:return BinDiv;
    case tok_percent:return BinMod;
    case tok_pow:  return BinPow;
    case tok_band: return BinBand;
    case tok_bor:  return BinBor;
    case tok_bxor: return BinBxor;
    default:       return BinEq;
    }
}

LogOp Parser::token_logop(TokenTag t) {
    switch (t) {
    case tok_land:    return LogAnd;
    case tok_lor:     return LogOr;
    case tok_nullish: return LogNullish;
    default:          return LogAnd;
    }
}

AsgnOp Parser::token_asgnop(TokenTag t) {
    switch (t) {
    case tok_assign:       return AsgnAssign;
    case tok_plus_assign:  return AsgnAdd;
    case tok_minus_assign: return AsgnSub;
    case tok_star_assign:  return AsgnMul;
    case tok_slash_assign: return AsgnDiv;
    case tok_pct_assign:   return AsgnMod;
    case tok_pow_assign:   return AsgnPow;
    case tok_band_assign:  return AsgnBand;
    case tok_bor_assign:   return AsgnBor;
    case tok_bxor_assign:  return AsgnBxor;
    case tok_shl_assign:   return AsgnShl;
    case tok_sar_assign:   return AsgnSar;
    case tok_shr_assign:   return AsgnShr;
    case tok_nullish_assign: return AsgnNullish;
    case tok_land_assign:  return AsgnLand;
    case tok_lor_assign:   return AsgnLor;
    default:               return AsgnAssign;
    }
}

UnOp Parser::token_unop(TokenTag t) {
    switch (t) {
    case tok_minus:  return UnMinus;
    case tok_plus:   return UnPlus;
    case tok_bang:   return UnBang;
    case tok_bnot:   return UnTilde;
    case tok_typeof: return UnTypeof;
    case tok_void:   return UnVoid;
    case tok_delete: return UnDelete;
    default:         return UnMinus;
    }
}

UpdOp Parser::token_updop(TokenTag t) {
    return t == tok_inc ? UpdInc : UpdDec;
}

// ─── Precedence helpers ──────────────────────────────────────────────────────

uint8_t Parser::infix_prec() const {
    auto tag = current_.tag;
    if ((tag == tok_inc || tag == tok_dec) && has_newline_before()) return 0;
    return tag_prec(tag);
}

uint8_t Parser::max_left_prec(NodeIndex n) const {
    switch (tree_.kind(n)) {
    case NK_ARROW_FUNCTION:
    case NK_YIELD_EXPR:
        return Prec::Comma;
    case NK_UPDATE_EXPR:
    case NK_UNARY_EXPR:
    case NK_AWAIT_EXPR:
    case NK_BINARY_EXPR:
    case NK_LOGICAL_EXPR:
    case NK_CONDITIONAL_EXPR:
    case NK_ASSIGNMENT_EXPR:
    case NK_SEQUENCE_EXPR:
        return Prec::Unary;
    default:
        return 255;
    }
}

// ─── Pratt expression parser ─────────────────────────────────────────────────

NodeIndex Parser::parse_expr(uint8_t min_prec) {
    NodeIndex left = parse_prefix();
    if (left == NodeNull) return NodeNull;

    while (true) {
        uint8_t prec = infix_prec();
        if (prec < min_prec || prec == 0) break;
        if (prec > max_left_prec(left)) break;
        if (!ctx_in_ && current_.tag == tok_in) break;

        left = parse_infix(prec, left);
        if (left == NodeNull) break;
    }
    return left;
}

// ─── Prefix dispatch ─────────────────────────────────────────────────────────

NodeIndex Parser::parse_prefix() {
    auto tag = current_.tag;
    uint32_t start = current_.start;

    if (tag == tok_ident) {
        advance();
        return tree_.alloc(NK_IDENT_REF, {start, prev_end_});
    }

    if (tag == tok_inc || tag == tok_dec) {
        UpdOp op = token_updop(tag);
        advance();
        NodeIndex arg = parse_expr(Prec::Unary);
        if (current_.tag == tok_pow) {
            error("unparenthesized unary expression cannot appear on left of '**'");
        }
        return tree_.alloc(NK_UPDATE_EXPR, span_from(start),
                           arg, op, NF::Prefix);
    }

    if (tag_is_unary(tag)) {
        UnOp op = token_unop(tag);
        advance();
        NodeIndex arg = parse_expr(Prec::Unary);
        if (current_.tag == tok_pow) {
            error("unparenthesized unary expression cannot appear on left of '**'");
        }
        return tree_.alloc(NK_UNARY_EXPR, span_from(start),
                           arg, op);
    }

    if (tag == tok_lparen) {
        return parse_paren_or_arrow();
    }

    if (tag == tok_await && ctx_await_) {
        advance();
        if (current_.tag == tok_semi || current_.tag == tok_rbrace ||
            current_.tag == tok_rbrack || current_.tag == tok_rparen ||
            current_.tag == tok_eof || has_newline_before()) {
            return tree_.alloc(NK_AWAIT_EXPR, span_from(start), NodeNull);
        }
        NodeIndex arg = parse_expr(Prec::Unary);
        if (current_.tag == tok_pow) {
            error("unparenthesized await expression cannot appear on left of '**'");
        }
        return tree_.alloc(NK_AWAIT_EXPR, span_from(start), arg);
    }

    if (tag == tok_yield && ctx_yield_) {
        advance();
        uint32_t delegate = 0;
        if (eat(tok_star)) delegate = NF::Delegate;
        NodeIndex arg = NodeNull;
        if (current_.tag != tok_semi && current_.tag != tok_rbrace &&
            current_.tag != tok_rbrack && current_.tag != tok_rparen &&
            current_.tag != tok_eof && !has_newline_before()) {
            arg = parse_expr(Prec::Assign);
        }
        return tree_.alloc(NK_YIELD_EXPR, span_from(start), arg, delegate);
    }

    if (tag == tok_new) {
        advance();
        if (current_.tag == tok_dot) {
            uint32_t new_end = prev_end_;
            advance();
            Token prop = expect(tok_ident);
            if (!source_eq(prop.start, prop.end, "target")) {
                error("the only valid meta property for 'new' is 'new.target'");
            }
            return tree_.alloc(NK_META_PROPERTY, span_from(start),
                               start, new_end, prop.start, prop.end);
        }
        NodeIndex callee = parse_expr(Prec::New);
        IndexRange args = {0, 0};
        // If parse_expr consumed a call (e.g. new (X)(Y) where (Y) was eaten as a call),
        // unwrap: (X)(Y) → callee=(X), args=(Y)
        if (tree_.kind(callee) == NK_CALL_EXPR) {
            args = tree_.range(callee, 1);
            callee = tree_.d(callee, 0);
        } else if (at(tok_lparen)) {
            advance();
            auto cp = static_cast<uint32_t>(scratch_a_.size());
            if (!at(tok_rparen)) {
                do {
                    if (at(tok_spread)) {
                        advance();
                        NodeIndex elem = parse_expr(Prec::Assign);
                        NodeIndex sp = tree_.alloc(NK_SPREAD, span_from(current_.start), elem);
                        scratch_a_.push_back(sp);
                    } else {
                        scratch_a_.push_back(parse_expr(Prec::Assign));
                    }
                } while (eat(tok_comma));
            }
            args = flush_scratch(scratch_a_, cp);
            expect(tok_rparen);
        }
        return tree_.alloc(NK_NEW_EXPR, span_from(start),
                           callee, args.start, args.len);
    }

    if (tag == tok_import) {
        advance();
        if (at(tok_dot)) {
            uint32_t import_end = prev_end_;
            advance();
            Token prop = expect(tok_ident);
            if (!source_eq(prop.start, prop.end, "meta")) {
                error("the only valid meta property for 'import' is 'import.meta'");
            }
            return tree_.alloc(NK_META_PROPERTY, span_from(start),
                               start, import_end, prop.start, prop.end);
        }
        expect(tok_lparen);
        NodeIndex src = parse_expr(Prec::Assign);
        NodeIndex opts = NodeNull;
        if (eat(tok_comma)) {
            opts = parse_expr(Prec::Assign);
        }
        expect(tok_rparen);
        return tree_.alloc(NK_IMPORT_EXPR, span_from(start), src, opts);
    }

    if (tag == tok_async) {
        Token async_tok = current_;
        advance();
        if (at(tok_function)) {
            advance();
            return parse_function(true, true);
        }
        if (at(tok_ident) && !current_.has_newline_before()) {
            Token id_tok = current_;
            advance();
            if (at(tok_arrow) && !has_newline_before()) {
                NodeIndex param = tree_.alloc(NK_BINDING_IDENT, {id_tok.start, id_tok.end});
                bool saved_await = ctx_await_;
                ctx_await_ = true;
                NodeIndex arrow = parse_simple_arrow(param);
                ctx_await_ = saved_await;
                tree_.d(arrow, 3) |= NF::Async;
                tree_.nodes[arrow].span.start = async_tok.start;
                return arrow;
            }
            return tree_.alloc(NK_IDENT_REF, {async_tok.start, async_tok.end});
        }
        if (at(tok_lparen) && !current_.has_newline_before()) {
            bool saved_await = ctx_await_;
            ctx_await_ = true;
            NodeIndex arrow = parse_paren_or_arrow();
            ctx_await_ = saved_await;
            if (tree_.kind(arrow) == NK_ARROW_FUNCTION) {
                tree_.d(arrow, 3) |= NF::Async;
                tree_.nodes[arrow].span.start = async_tok.start;
            }
            return arrow;
        }
        return tree_.alloc(NK_IDENT_REF, {async_tok.start, async_tok.end});
    }

    if (tag == tok_function) {
        advance();
        return parse_function(true);
    }

    if (tag == tok_class) {
        advance();
        return parse_class(true);
    }

    return parse_primary();
}

// ─── Infix dispatch ──────────────────────────────────────────────────────────

NodeIndex Parser::parse_infix(uint8_t prec, NodeIndex left) {
    auto tag = current_.tag;
    uint32_t start = current_.start;

    if (tag_is_binary(tag)) {
        BinOp op = token_binop(tag);
        advance();
        uint8_t next_prec = prec;
        if (op == BinPow) next_prec = prec - 1;
        else next_prec = prec;
        NodeIndex right = parse_expr(next_prec);
        return tree_.alloc(NK_BINARY_EXPR, span_from(start), left, right, op);
    }

    if (tag_is_logical(tag)) {
        LogOp op = token_logop(tag);
        if (op == LogNullish && kind(left) == NK_LOGICAL_EXPR &&
            tree_.d(left, 2) != LogNullish) {
            error("nullish coalescing cannot be mixed with logical operators without parentheses");
        } else if (op != LogNullish && kind(left) == NK_LOGICAL_EXPR &&
                   tree_.d(left, 2) == LogNullish) {
            error("nullish coalescing cannot be mixed with logical operators without parentheses");
        }
        advance();
        NodeIndex right = parse_expr(prec);
        if (op == LogNullish && kind(right) == NK_LOGICAL_EXPR &&
            tree_.d(right, 2) != LogNullish) {
            error("nullish coalescing cannot be mixed with logical operators without parentheses");
        } else if (op != LogNullish && kind(right) == NK_LOGICAL_EXPR &&
                   tree_.d(right, 2) == LogNullish) {
            error("nullish coalescing cannot be mixed with logical operators without parentheses");
        }
        return tree_.alloc(NK_LOGICAL_EXPR, span_from(start), left, right, op);
    }

    if (tag_is_assign(tag)) {
        AsgnOp op = token_asgnop(tag);
        advance();
        if (op == AsgnAssign) {
            left = expr_to_binding(left);
            cover_has_init_name_ = false;
        } else {
            if (!is_simple_assign_target(left)) {
                error("invalid assignment target");
            }
        }
        NodeIndex right = parse_expr(Prec::Assign - 1);
        return tree_.alloc(NK_ASSIGNMENT_EXPR, span_from(start), left, right, op);
    }

    if (tag == tok_question) {
        advance();
        NodeIndex consequent = parse_expr(Prec::Assign);
        expect(tok_colon);
        NodeIndex alternate = parse_expr(Prec::Assign);
        return tree_.alloc(NK_CONDITIONAL_EXPR, span_from(start),
                           left, consequent, alternate);
    }

    if (tag == tok_comma) {
        auto cp = static_cast<uint32_t>(scratch_a_.size());
        scratch_a_.push_back(left);
        advance();
        do {
            scratch_a_.push_back(parse_expr(Prec::Assign));
        } while (eat(tok_comma) && tag_prec(tok_comma) >= prec);
        IndexRange exprs = flush_scratch(scratch_a_, cp);
        return tree_.alloc(NK_SEQUENCE_EXPR,
                           {tree_.span(left).start, prev_end_},
                           exprs.start, exprs.len);
    }

    if (tag == tok_arrow && !has_newline_before()) {
        return parse_simple_arrow(left);
    }

    if (tag == tok_inc || tag == tok_dec) {
        UpdOp op = token_updop(tag);
        advance();
        return tree_.alloc(NK_UPDATE_EXPR, span_from(start), left, op, 0);
    }

    if (tag == tok_dot) {
        advance();
        Token prop = current_;
        if (is_ident_like()) advance();
        else error("expected property name");
        NodeIndex prop_node = tree_.alloc(NK_IDENT_REF, {prop.start, prev_end_});
        NodeIndex mem = tree_.alloc(NK_MEMBER_EXPR, span_from(start),
                           left, prop_node, 0);
        return wrap_chain(left, mem, span_from(start));
    }

    if (tag == tok_lbrack) {
        advance();
        bool saved_in = ctx_in_;
        ctx_in_ = true;
        NodeIndex prop = parse_expr();
        ctx_in_ = saved_in;
        expect(tok_rbrack);
        NodeIndex mem = tree_.alloc(NK_MEMBER_EXPR, span_from(start),
                           left, prop, NF::Computed);
        return wrap_chain(left, mem, span_from(start));
    }

    if (tag == tok_lparen) {
        advance();
        auto cp = static_cast<uint32_t>(scratch_a_.size());
        if (!at(tok_rparen)) {
            do {
                if (at(tok_spread)) {
                    advance();
                    NodeIndex elem = parse_expr(Prec::Assign);
                    NodeIndex sp = tree_.alloc(NK_SPREAD, span_from(current_.start), elem);
                    scratch_a_.push_back(sp);
                } else {
                    scratch_a_.push_back(parse_expr(Prec::Assign));
                }
            } while (eat(tok_comma));
        }
        IndexRange args = flush_scratch(scratch_a_, cp);
        expect(tok_rparen);
        NodeIndex call = tree_.alloc(NK_CALL_EXPR, span_from(start),
                           left, args.start, args.len);
        return wrap_chain(left, call, span_from(start));
    }

    if (tag == tok_opt_chain) {
        advance();
        NodeIndex chain;
        if (at(tok_lbrack)) {
            advance();
            bool saved_in = ctx_in_;
            ctx_in_ = true;
            NodeIndex prop = parse_expr();
            ctx_in_ = saved_in;
            expect(tok_rbrack);
            NodeIndex member = tree_.alloc(NK_MEMBER_EXPR, span_from(start),
                                           left, prop, NF::Computed | NF::Optional);
            chain = member;
        } else if (at(tok_lparen)) {
            advance();
            auto cp = static_cast<uint32_t>(scratch_a_.size());
            if (!at(tok_rparen)) {
                do { scratch_a_.push_back(parse_expr(Prec::Assign)); } while (eat(tok_comma));
            }
            IndexRange args = flush_scratch(scratch_a_, cp);
            expect(tok_rparen);
            NodeIndex call = tree_.alloc(NK_CALL_EXPR, span_from(start),
                                         left, args.start, args.len, NF::Optional);
            chain = call;
        } else {
            Token prop = current_;
            if (is_ident_like()) advance();
            else error("expected property name");
            NodeIndex prop_node = tree_.alloc(NK_IDENT_REF, {prop.start, prev_end_});
            NodeIndex member = tree_.alloc(NK_MEMBER_EXPR, span_from(start),
                                           left, prop_node, NF::Optional);
            chain = member;
        }
        return tree_.alloc(NK_CHAIN_EXPR, span_from(start), left, chain);
    }

    if (tag == tok_template_full || tag == tok_template_head) {
        Span opener = {current_.start, current_.end};
        advance();
        return tree_.alloc(NK_TAGGED_TEMPLATE, span_from(start), left,
                           parse_template_lit(opener));
    }

    error("unexpected token in expression");
    advance();
    return left;
}

} // namespace qjsp
