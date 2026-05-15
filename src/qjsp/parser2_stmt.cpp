#include "qjsp/parser2.hpp"

namespace qjsp {

// ─── Top-level parse ─────────────────────────────────────────────────────────

void Parser::init(const uint8_t *source, uint32_t len) {
    lexer_.init(source, len, true);
    tree_.init(source, len);
}

NodeIndex Parser::parse() {
    current_ = lexer_.next_token();
    ctx_await_ = true;
    ctx_return_ = false;
    NodeIndex program = parse_stmt_list_body();
    tree_.root = program;
    return program;
}

NodeIndex Parser::parse_stmt_list_body() {
    uint32_t start = current_.start;
    auto cp = static_cast<uint32_t>(scratch_stmts_.size());

    while (!at(tok_eof)) {
        NodeIndex s = parse_stmt();
        if (s != NodeNull) scratch_stmts_.push_back(s);
        else advance();
    }

    IndexRange body = flush_scratch(scratch_stmts_, cp);
    return tree_.alloc(NK_PROGRAM, span_from(start), body.start, body.len);
}

// ─── Statement dispatch ──────────────────────────────────────────────────────

NodeIndex Parser::parse_stmt() {
    auto tag = current_.tag;

    switch (tag) {
    case tok_lbrace:    return parse_block();
    case tok_if:        return parse_if_stmt();
    case tok_switch:    return parse_switch_stmt();
    case tok_for:       return parse_for_stmt();
    case tok_while:     return parse_while_stmt();
    case tok_do:        return parse_do_while_stmt();
    case tok_with:      return parse_with_stmt();
    case tok_break:     return parse_break_stmt();
    case tok_continue:  return parse_continue_stmt();
    case tok_return:    return parse_return_stmt();
    case tok_throw:     return parse_throw_stmt();
    case tok_try:       return parse_try_stmt();
    case tok_debugger:  return parse_debugger_stmt();
    case tok_semi:      return parse_empty_stmt();
    case tok_var:
    case tok_let:
    case tok_const:     return parse_var_decl();
    case tok_function:  advance(); return parse_function(false);
    case tok_async: {
        if (current_.has_newline_before()) break;
        uint32_t as = current_.start;
        advance();
        if (at(tok_function)) {
            advance();
            NodeIndex fn = parse_function(false);
            tree_.d(fn, 3) |= NF::Async;
            tree_.nodes[fn].span.start = as;
            return fn;
        }
        NodeIndex async_id = tree_.alloc(NK_IDENT_REF, {as, prev_end_});
        if (at(tok_ident) && !has_newline_before()) {
            Token id_tok = current_;
            advance();
            if (at(tok_arrow) && !has_newline_before()) {
                NodeIndex param = tree_.alloc(NK_BINDING_IDENT, {id_tok.start, id_tok.end});
                bool saved_await = ctx_await_;
                ctx_await_ = true;
                NodeIndex arrow = parse_simple_arrow(param);
                ctx_await_ = saved_await;
                tree_.d(arrow, 3) |= NF::Async;
                tree_.nodes[arrow].span.start = as;
                eat_semi();
                return tree_.alloc(NK_EXPR_STMT, span_from(as), arrow);
            }
            NodeIndex id_ref = tree_.alloc(NK_IDENT_REF, {id_tok.start, prev_end_});
            NodeIndex seq = tree_.alloc(NK_SEQUENCE_EXPR, {as, prev_end_}, 0, 0);
            eat_semi();
            return tree_.alloc(NK_EXPR_STMT, span_from(as), seq);
        }
        if (at(tok_lparen) && !has_newline_before()) {
            bool saved_await = ctx_await_;
            ctx_await_ = true;
            NodeIndex arrow = parse_paren_or_arrow();
            ctx_await_ = saved_await;
            if (tree_.kind(arrow) == NK_ARROW_FUNCTION) {
                tree_.d(arrow, 3) |= NF::Async;
                tree_.nodes[arrow].span.start = as;
            }
            eat_semi();
            return tree_.alloc(NK_EXPR_STMT, span_from(as), arrow);
        }
        eat_semi();
        return tree_.alloc(NK_EXPR_STMT, span_from(as), async_id);
    }
    case tok_class:     advance(); return parse_class(false);
    case tok_import: {
        uint32_t import_start = current_.start;
        advance();
        if (at(tok_dot)) {
            advance();
            Token prop = expect(tok_ident);
            if (!source_eq(prop.start, prop.end, "meta")) {
                error("the only valid meta property for 'import' is 'import.meta'");
            }
            NodeIndex meta = tree_.alloc(NK_META_PROPERTY, span_from(import_start),
                                         import_start, prev_end_, prop.start, prop.end);
            eat_semi();
            return tree_.alloc(NK_EXPR_STMT, span_from(import_start), meta);
        }
        if (at(tok_lparen)) {
            advance();
            NodeIndex arg = parse_expr(Prec::Assign);
            expect(tok_rparen);
            return tree_.alloc(NK_IMPORT_EXPR, span_from(import_start), arg);
        }
        return parse_import_decl();
    }
    case tok_export:    advance(); return parse_export_decl();
    default:            return parse_expr_or_label_or_directive();
    }
}

NodeIndex Parser::parse_block() {
    uint32_t start = current_.start;
    expect(tok_lbrace);
    auto cp = static_cast<uint32_t>(scratch_stmts_.size());

    while (!at(tok_rbrace) && !at(tok_eof)) {
        NodeIndex s = parse_stmt();
        if (s != NodeNull) scratch_stmts_.push_back(s);
        else advance();
    }

    IndexRange body = flush_scratch(scratch_stmts_, cp);
    expect(tok_rbrace);
    return tree_.alloc(NK_BLOCK_STMT, span_from(start), body.start, body.len);
}

NodeIndex Parser::parse_expr_or_label_or_directive() {
    uint32_t start = current_.start;
    NodeIndex expr = parse_expr();
    if (expr == NodeNull) return NodeNull;

    if (at(tok_colon) && tree_.kind(expr) == NK_IDENT_REF && !has_newline_before()) {
        advance();
        NodeIndex body = parse_stmt();
        return tree_.alloc(NK_LABELED_STMT, span_from(start), expr, body);
    }

    eat_semi();
    return tree_.alloc(NK_EXPR_STMT, span_from(start), expr);
}

NodeIndex Parser::parse_if_stmt() {
    uint32_t start = current_.start;
    advance();
    expect(tok_lparen);
    NodeIndex test = parse_expr();
    expect(tok_rparen);
    NodeIndex consequent = parse_stmt();
    NodeIndex alternate = NodeNull;
    if (eat(tok_else)) {
        alternate = parse_stmt();
    }
    return tree_.alloc(NK_IF_STMT, span_from(start), test, consequent, alternate);
}

NodeIndex Parser::parse_switch_stmt() {
    uint32_t start = current_.start;
    advance();
    expect(tok_lparen);
    NodeIndex disc = parse_expr();
    expect(tok_rparen);
    expect(tok_lbrace);
    IndexRange cases = parse_switch_cases();
    expect(tok_rbrace);
    return tree_.alloc(NK_SWITCH_STMT, span_from(start), disc,
                       cases.start, cases.len);
}

IndexRange Parser::parse_switch_cases() {
    auto cp = static_cast<uint32_t>(scratch_a_.size());
    while (!at(tok_rbrace) && !at(tok_eof)) {
        uint32_t case_start = current_.start;
        NodeIndex test = NodeNull;
        if (eat(tok_case)) {
            test = parse_expr();
        } else {
            expect(tok_default);
        }
        expect(tok_colon);
        auto body_cp = static_cast<uint32_t>(scratch_b_.size());
        while (!at(tok_case) && !at(tok_default) && !at(tok_rbrace) && !at(tok_eof)) {
            NodeIndex s = parse_stmt();
            if (s != NodeNull) scratch_b_.push_back(s);
            else advance();
        }
        IndexRange body = flush_scratch(scratch_b_, body_cp);
        NodeIndex c = tree_.alloc(NK_SWITCH_CASE, span_from(case_start),
                                  test, body.start, body.len);
        scratch_a_.push_back(c);
    }
    return flush_scratch(scratch_a_, cp);
}

NodeIndex Parser::parse_for_stmt() {
    advance();

    bool is_await = false;
    if (at(tok_await) && !has_newline_before()) {
        is_await = true;
        advance();
    }

    expect(tok_lparen);

    bool saved_in = ctx_in_;
    ctx_in_ = false;

    NodeIndex init = NodeNull;
    bool init_is_decl = false;

    if (at(tok_var) || at(tok_let) || at(tok_const)) {
        init = parse_var_decl();
        init_is_decl = true;
    } else if (!at(tok_semi)) {
        init = parse_expr();
    }

    NodeIndex result = parse_for_rest(init, init_is_decl, is_await);

    ctx_in_ = saved_in;
    expect(tok_rparen);
    NodeIndex body = parse_stmt();
    tree_.d(result, 3) = body;
    tree_.nodes[result].span.end = prev_end_;
    return result;
}

NodeIndex Parser::parse_for_rest(NodeIndex init, bool init_is_decl, bool is_await) {
    if (at(tok_in)) {
        if (is_await) error("'for await' requires 'of', not 'in'");
        advance();
        NodeIndex right = parse_expr();
        uint32_t kind = NK_FOR_IN_STMT;
        return tree_.alloc(static_cast<NodeKind>(kind), cur_span(), init, right);
    }
    if (at(tok_of)) {
        advance();
        NodeIndex right = parse_expr();
        uint32_t flags = is_await ? NF::Await : 0;
        return tree_.alloc(NK_FOR_OF_STMT, cur_span(), init, right, flags);
    }
    if (is_await) error("'for await' is only valid with for-of loops");
    if (init_is_decl && init != NodeNull) {
        validate_for_init_declarators(init);
    }
    expect(tok_semi);
    NodeIndex test = NodeNull;
    if (!at(tok_semi)) test = parse_expr();
    expect(tok_semi);
    NodeIndex update = NodeNull;
    if (!at(tok_rparen)) update = parse_expr();
    return tree_.alloc(NK_FOR_STMT, cur_span(), init, test, update);
}

NodeIndex Parser::parse_while_stmt() {
    uint32_t start = current_.start;
    advance();
    expect(tok_lparen);
    NodeIndex test = parse_expr();
    expect(tok_rparen);
    NodeIndex body = parse_stmt();
    return tree_.alloc(NK_WHILE_STMT, span_from(start), test, body);
}

NodeIndex Parser::parse_do_while_stmt() {
    uint32_t start = current_.start;
    advance();
    NodeIndex body = parse_stmt();
    expect(tok_while);
    expect(tok_lparen);
    NodeIndex test = parse_expr();
    expect(tok_rparen);
    eat_semi();
    return tree_.alloc(NK_DO_WHILE_STMT, span_from(start), test, body);
}

NodeIndex Parser::parse_with_stmt() {
    uint32_t start = current_.start;
    advance();
    error("'with' statement not allowed in strict mode");
    expect(tok_lparen);
    NodeIndex obj = parse_expr();
    expect(tok_rparen);
    NodeIndex body = parse_stmt();
    return tree_.alloc(NK_WITH_STMT, span_from(start), obj, body);
}

NodeIndex Parser::parse_break_stmt() {
    uint32_t start = current_.start;
    advance();
    NodeIndex label = NodeNull;
    if (is_ident_like() && !has_newline_before()) {
        label = tree_.alloc(NK_LABEL_IDENT, cur_span());
        advance();
    }
    eat_semi();
    return tree_.alloc(NK_BREAK_STMT, span_from(start), label);
}

NodeIndex Parser::parse_continue_stmt() {
    uint32_t start = current_.start;
    advance();
    NodeIndex label = NodeNull;
    if (is_ident_like() && !has_newline_before()) {
        label = tree_.alloc(NK_LABEL_IDENT, cur_span());
        advance();
    }
    eat_semi();
    return tree_.alloc(NK_CONTINUE_STMT, span_from(start), label);
}

NodeIndex Parser::parse_return_stmt() {
    uint32_t start = current_.start;
    if (!ctx_return_) error("illegal return statement");
    advance();
    NodeIndex arg = NodeNull;
    if (!at(tok_semi) && !at(tok_rbrace) && !at(tok_eof) && !has_newline_before()) {
        arg = parse_expr();
    }
    eat_semi();
    return tree_.alloc(NK_RETURN_STMT, span_from(start), arg);
}

NodeIndex Parser::parse_throw_stmt() {
    uint32_t start = current_.start;
    advance();
    if (has_newline_before()) {
        error("illegal newline after throw");
        return NodeNull;
    }
    NodeIndex arg = parse_expr();
    eat_semi();
    return tree_.alloc(NK_THROW_STMT, span_from(start), arg);
}

NodeIndex Parser::parse_try_stmt() {
    uint32_t start = current_.start;
    advance();
    NodeIndex block = parse_block();
    NodeIndex handler = NodeNull;
    NodeIndex finalizer = NodeNull;
    if (at(tok_catch)) {
        handler = parse_catch();
    }
    if (eat(tok_finally)) {
        finalizer = parse_block();
    }
    if (handler == NodeNull && finalizer == NodeNull) {
        error("expected 'catch' or 'finally'");
    }
    return tree_.alloc(NK_TRY_STMT, span_from(start), block, handler, finalizer);
}

NodeIndex Parser::parse_catch() {
    uint32_t start = current_.start;
    advance();
    NodeIndex param = NodeNull;
    if (eat(tok_lparen)) {
        param = parse_binding();
        expect(tok_rparen);
    }
    NodeIndex body = parse_block();
    return tree_.alloc(NK_CATCH_CLAUSE, span_from(start), param, body);
}

NodeIndex Parser::parse_debugger_stmt() {
    uint32_t start = current_.start;
    advance();
    eat_semi();
    return tree_.alloc(NK_DEBUGGER_STMT, span_from(start));
}

NodeIndex Parser::parse_empty_stmt() {
    uint32_t start = current_.start;
    advance();
    return tree_.alloc(NK_EMPTY_STMT, span_from(start));
}

NodeIndex Parser::parse_var_decl() {
    uint32_t start = current_.start;
    uint32_t kind = VarVar;
    if (at(tok_let)) kind = VarLet;
    else if (at(tok_const)) kind = VarConst;
    advance();

    auto cp = static_cast<uint32_t>(scratch_a_.size());
    do {
        uint32_t decl_start = current_.start;
        NodeIndex id = parse_binding();
        NodeIndex init = NodeNull;
        if (eat(tok_assign)) {
            init = parse_expr(Prec::Assign);
        }
        NodeIndex declarator = tree_.alloc(NK_VAR_DECLARATOR, span_from(decl_start),
                                           id, init);
        scratch_a_.push_back(declarator);
    } while (eat(tok_comma));

    if (!at(tok_rparen)) eat_semi();

    IndexRange decls = flush_scratch(scratch_a_, cp);
    return tree_.alloc(NK_VAR_DECL, span_from(start), decls.start, decls.len, kind);
}

// ─── Import declarations ──────────────────────────────────────────────────────

NodeIndex Parser::parse_import_decl() {
    uint32_t start = prev_end_;

    if (at(tok_string)) {
        Token src = advance();
        NodeIndex src_node = tree_.alloc(NK_STRING_LIT, {src.start, src.end});
        eat_semi();
        return tree_.alloc(NK_IMPORT_DECL, span_from(start),
                           0, 0, src_node);
    }

    auto cp = static_cast<uint32_t>(scratch_a_.size());

    if (at(tok_star)) {
        advance();
        expect(tok_as);
        NodeIndex local = parse_binding();
        NodeIndex ns = tree_.alloc(NK_IMPORT_NAMESPACE, tree_.span(local), local);
        scratch_a_.push_back(ns);
    } else if (at(tok_lbrace)) {
        IndexRange named = parse_named_imports();
        for (uint32_t i = 0; i < named.len; i++)
            scratch_a_.push_back(tree_.extra(named)[i]);
    } else {
        NodeIndex local = parse_binding();
        NodeIndex def = tree_.alloc(NK_IMPORT_DEFAULT, tree_.span(local), local);
        scratch_a_.push_back(def);

        if (eat(tok_comma)) {
            if (at(tok_star)) {
                advance();
                expect(tok_as);
                NodeIndex ns_local = parse_binding();
                NodeIndex ns = tree_.alloc(NK_IMPORT_NAMESPACE, tree_.span(ns_local), ns_local);
                scratch_a_.push_back(ns);
            } else if (at(tok_lbrace)) {
                IndexRange named = parse_named_imports();
                for (uint32_t i = 0; i < named.len; i++)
                    scratch_a_.push_back(tree_.extra(named)[i]);
            }
        }
    }

    IndexRange specs = flush_scratch(scratch_a_, cp);

    if (!at(tok_from) && !at(tok_string)) {
        if (specs.len > 0 && tag_is_ident_like(current_.tag)) {
            error("expected 'from' before module specifier");
        }
    } else {
        if (at(tok_from)) advance();
    }

    NodeIndex source = NodeNull;
    if (at(tok_string)) {
        Token src = advance();
        source = tree_.alloc(NK_STRING_LIT, {src.start, src.end});
    } else {
        error("expected module specifier string");
    }

    eat_semi();
    return tree_.alloc(NK_IMPORT_DECL, span_from(start),
                       specs.start, specs.len, source);
}

IndexRange Parser::parse_named_imports() {
    expect(tok_lbrace);
    auto cp = static_cast<uint32_t>(scratch_a_.size());

    while (!at(tok_rbrace) && !at(tok_eof)) {
        scratch_a_.push_back(parse_import_spec());
        if (!eat(tok_comma)) break;
    }

    expect(tok_rbrace);
    return flush_scratch(scratch_a_, cp);
}

NodeIndex Parser::parse_import_spec() {
    Token imported_tok = current_;
    if (!is_ident_like() && current_.tag != tok_string) {
        error("expected import specifier name");
        advance();
        return NodeNull;
    }
    advance();

    NodeIndex imported = tree_.alloc(NK_IDENT_REF, {imported_tok.start, prev_end_});

    NodeIndex local = imported;
    if (eat(tok_as)) {
        local = parse_binding();
    }

    return tree_.alloc(NK_IMPORT_SPEC, span_from(imported_tok.start),
                       imported, local);
}

// ─── Export declarations ──────────────────────────────────────────────────────

NodeIndex Parser::parse_export_decl() {
    uint32_t start = prev_end_;

    if (at(tok_default)) {
        advance();
        return parse_export_default();
    }

    if (at(tok_star)) {
        advance();
        return parse_export_all();
    }

    if (at(tok_lbrace)) {
        return parse_export_named();
    }

    NodeIndex decl = NodeNull;
    switch (current_.tag) {
    case tok_var:
    case tok_let:
    case tok_const:
        decl = parse_var_decl();
        break;
    case tok_function:
        advance();
        decl = parse_function(false);
        break;
    case tok_async: {
        uint32_t as = current_.start;
        advance();
        if (at(tok_function)) {
            advance();
            decl = parse_function(false);
            tree_.d(decl, 3) |= NF::Async;
            tree_.nodes[decl].span.start = as;
        }
        break;
    }
    case tok_class:
        advance();
        decl = parse_class(false);
        break;
    default:
        error("expected declaration after export");
        break;
    }

    return tree_.alloc(NK_EXPORT_NAMED, span_from(start),
                       decl, 0, 0, NodeNull);
}

NodeIndex Parser::parse_export_default() {
    uint32_t start = prev_end_;

    NodeIndex decl = NodeNull;

    if (at(tok_function)) {
        advance();
        decl = parse_function(false);
    } else if (at(tok_async) && !has_newline_before()) {
        advance();
        if (at(tok_function)) {
            advance();
            decl = parse_function(false);
            tree_.d(decl, 3) |= NF::Async;
        }
    } else if (at(tok_class)) {
        advance();
        decl = parse_class(false);
    } else {
        decl = parse_expr(Prec::Assign);
        eat_semi();
    }

    return tree_.alloc(NK_EXPORT_DEFAULT, span_from(start), decl);
}

NodeIndex Parser::parse_export_named() {
    uint32_t start = prev_end_;

    expect(tok_lbrace);
    auto cp = static_cast<uint32_t>(scratch_a_.size());

    while (!at(tok_rbrace) && !at(tok_eof)) {
        scratch_a_.push_back(parse_export_spec());
        if (!eat(tok_comma)) break;
    }

    expect(tok_rbrace);
    IndexRange specs = flush_scratch(scratch_a_, cp);

    NodeIndex source = NodeNull;
    if (eat(tok_from)) {
        if (at(tok_string)) {
            Token src = advance();
            source = tree_.alloc(NK_STRING_LIT, {src.start, src.end});
        } else {
            error("expected module specifier string");
        }
    }

    eat_semi();
    return tree_.alloc(NK_EXPORT_NAMED, span_from(start),
                       NodeNull, specs.start, specs.len, source);
}

NodeIndex Parser::parse_export_all() {
    uint32_t start = prev_end_;

    NodeIndex exported = NodeNull;
    if (eat(tok_as)) {
        if (is_ident_like()) {
            Token exp_tok = advance();
            exported = tree_.alloc(NK_IDENT_REF, {exp_tok.start, exp_tok.end});
        } else {
            error("expected identifier after 'as'");
        }
    }

    expect(tok_from);

    NodeIndex source = NodeNull;
    if (at(tok_string)) {
        Token src = advance();
        source = tree_.alloc(NK_STRING_LIT, {src.start, src.end});
    } else {
        error("expected module specifier string");
    }

    eat_semi();
    return tree_.alloc(NK_EXPORT_ALL, span_from(start), exported, source);
}

NodeIndex Parser::parse_export_spec() {
    Token local_tok = current_;
    if (!is_ident_like() && current_.tag != tok_string) {
        error("expected export specifier name");
        advance();
        return NodeNull;
    }
    advance();

    NodeIndex local = tree_.alloc(NK_IDENT_REF, {local_tok.start, prev_end_});

    NodeIndex exported = local;
    if (eat(tok_as)) {
        if (is_ident_like() || current_.tag == tok_string) {
            Token exp_tok = advance();
            exported = tree_.alloc(NK_IDENT_REF, {exp_tok.start, exp_tok.end});
        } else {
            error("expected name after 'as'");
        }
    }

    return tree_.alloc(NK_EXPORT_SPEC, span_from(local_tok.start),
                       exported, local);
}

void Parser::validate_for_init_declarators(NodeIndex decl) {
    uint32_t var_kind = tree_.d(decl, 2);
    IndexRange declarators = tree_.range(decl, 0);
    for (uint32_t i = 0; i < declarators.len; i++) {
        NodeIndex declarator = tree_.extra(declarators)[i];
        NodeIndex id = tree_.d(declarator, 0);
        NodeIndex init = tree_.d(declarator, 1);
        if (var_kind == VarConst && init == NodeNull) {
            error_at(tree_.span(declarator), "'const' declarations in for loop initializer must be initialized");
        }
        NodeKind id_kind = kind(id);
        if ((id_kind == NK_ARRAY_PAT || id_kind == NK_OBJECT_PAT) && init == NodeNull) {
            error_at(tree_.span(declarator), "destructuring declaration in for loop initializer must be initialized");
        }
    }
}

} // namespace qjsp
