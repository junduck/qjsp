#include "qjsp/context.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace qjsp {
// ═══════════════════════════════════════════════════════════════════════════
//  Statement parser
// ═══════════════════════════════════════════════════════════════════════════

void RegParseState::parse_statement() { parse_statement_or_decl(0); }

void RegParseState::parse_statement_or_decl(int decl_mask) {
  // Named label: ident : statement
  if (lexer.token.kind == TokenKind::Identifier && !lexer.token.ident_is_reserved && peek_token(true) == TokenKind::Colon) {
    Atom label = lexer.token.ident_atom;
    next_token(); // skip ident
    next_token(); // skip ':'

    bool is_loop = (lexer.token.kind == TokenKind::KwFor || lexer.token.kind == TokenKind::KwWhile || lexer.token.kind == TokenKind::KwDo);

    if (is_loop) {
      pending_label = label;
      if (lexer.token.kind == TokenKind::KwFor)
        parse_for_statement();
      else if (lexer.token.kind == TokenKind::KwWhile)
        parse_while_statement();
      else
        parse_do_statement();
      pending_label = kAtomNull;
    } else {
      BlockEnv be;
      int lbreak = new_label();
      cur_func->push_break(&be, label, lbreak, -1);
      parse_statement();
      emit_label(lbreak);
      cur_func->pop_break();
    }
    return;
  }

  TokenKind tok = lexer.token.kind;

  switch (tok) {
  case TokenKind::LBrace:
    parse_block();
    return;
  case TokenKind::KwIf:
    parse_if_statement();
    return;
  case TokenKind::KwReturn:
    parse_return_statement();
    return;
  case TokenKind::KwThrow:
    parse_throw_statement();
    return;
  case TokenKind::KwWhile:
    parse_while_statement();
    return;
  case TokenKind::KwFor:
    parse_for_statement();
    return;
  case TokenKind::KwDo:
    parse_do_statement();
    return;
  case TokenKind::KwBreak:
    parse_break_continue(false);
    return;
  case TokenKind::KwContinue:
    parse_break_continue(true);
    return;
  case TokenKind::KwSwitch:
    parse_switch_statement();
    return;
  case TokenKind::KwTry: {
    parse_try_statement();
    return;
  }

  case TokenKind::KwLet:
  case TokenKind::KwConst:
    if (!(decl_mask & 1)) {
      parse_expr_statement();
      return;
    }
  case TokenKind::KwVar: {
    next_token();
    parse_var_decls(tok);
    if (lexer.token.kind == TokenKind::Semicolon)
      next_token();
    return;
  }

  case TokenKind::KwFunction: {
    next_token();
    Atom name = kAtomNull;
    if (lexer.token.kind == TokenKind::Identifier) {
      name = lexer.token.ident_atom;
      next_token();
    }
    FunctionDef *fd = parse_function_decl(name, false, FunctionKind::normal);
    if (!fd)
      return;

    if (name != kAtomNull) {
      int var_idx = cur_func->find_var(name);
      if (var_idx < 0) {
        js_define_var(name, TokenKind::KwVar);
        var_idx = cur_func->find_var(name);
      }
      int r = alloc_temp();
      emit_iABx(RegOp::FCLOSURE, static_cast<uint8_t>(r), static_cast<uint16_t>(fd->parent_cpool_idx));
      emit_lvalue_store({LValue::LOCAL, -1, name, -1, var_idx}, {r});
      free_temp();
    }
    return;
  }

  case TokenKind::Semicolon:
    next_token();
    return;

  default:
    parse_expr_statement();
    return;
  }
}

void RegParseState::parse_try_statement() {
  next_token(); // skip 'try'

  constexpr int kExcReg = 200;

  int exc_reg        = kExcReg;
  int finalize_label = new_label();
  int catch_label    = new_label();
  int after_label    = new_label();

  TryInfo ti;
  ti.exc_reg       = exc_reg;
  ti.scope_level   = cur_func->scope_level;
  ti.finally_label = finalize_label;
  try_stack_.push_back(ti);

  cur_func->emit_jump(RegOp::CATCH, catch_label, static_cast<uint8_t>(exc_reg));

  expect(TokenKind::LBrace);
  while (lexer.token.kind != TokenKind::RBrace) {
    parse_statement_or_decl(1);
  }
  next_token();

  bool has_finally = false;
  if (lexer.token.kind == TokenKind::KwFinally) {
    has_finally = true;
    cur_func->emit_jump(RegOp::GOSUB, finalize_label, 0);
  }
  emit_iABx(RegOp::UNCATCH, 0, 0);
  int skip_catch_label = new_label();
  emit_jump(RegOp::JMP, skip_catch_label, 0);

  emit_label(catch_label);

  if (lexer.token.kind == TokenKind::KwCatch) {
    next_token();
    if (lexer.token.kind == TokenKind::LParen) {
      next_token();
      if (lexer.token.kind == TokenKind::Identifier) {
        Atom catch_name = lexer.token.ident_atom;
        next_token();
        expect(TokenKind::RParen);
        push_enter_scope();
        js_define_var(catch_name, TokenKind::KwLet);
        LValue catch_lv;
        for (int i = 0; i < cur_func->var_count; i++) {
          if (cur_func->vars[static_cast<size_t>(i)].var_name == catch_name &&
              cur_func->vars[static_cast<size_t>(i)].scope_level == cur_func->scope_level) {
            catch_lv.kind    = LValue::LOCAL;
            catch_lv.var_idx = i;
            break;
          }
        }
        emit_lvalue_store(catch_lv, {exc_reg});
      } else if (lexer.token.kind == TokenKind::RParen) {
        next_token();
      }
      expect(TokenKind::LBrace);
      while (lexer.token.kind != TokenKind::RBrace) {
        parse_statement_or_decl(1);
      }
      next_token();
      pop_leave_scope();
    }
    if (lexer.token.kind == TokenKind::KwFinally) {
      has_finally = true;
      cur_func->emit_jump(RegOp::GOSUB, finalize_label, 0);
    }
  } else if (has_finally) {
    cur_func->emit_jump(RegOp::GOSUB, finalize_label, 0);
    emit_iABC(RegOp::THROW, static_cast<uint8_t>(exc_reg), 0, 0);
  }
  emit_iABx(RegOp::UNCATCH, 0, 0);

  emit_label(skip_catch_label);
  emit_jump(RegOp::JMP, after_label, 0);

  try_stack_.pop_back();

  if (has_finally) {
    next_token(); // skip 'finally'
    emit_label(finalize_label);
    expect(TokenKind::LBrace);
    while (lexer.token.kind != TokenKind::RBrace) {
      parse_statement_or_decl(1);
    }
    next_token();
    emit_iABx(RegOp::RET, 0, 0);
  }

  emit_label(after_label);
  cur_func->alloc.ensure_max(exc_reg + 1);
}

void RegParseState::parse_switch_statement() {
  next_token();

  expect(TokenKind::LParen);
  RegSlot expr = parse_expr();
  expect(TokenKind::RParen);
  expect(TokenKind::LBrace);

  int label_end   = new_label();
  int label_chain = new_label();

  BlockEnv be;
  cur_func->push_break(&be, kAtomNull, label_end, -1);

  emit_jump(RegOp::JMP, label_chain, 0);

  struct CaseInfo {
    int body_lab;
    int cpool_idx = -1;
  };
  std::vector<CaseInfo> cases;

  while (lexer.token.kind != TokenKind::RBrace) {
    if (lexer.token.kind == TokenKind::KwCase || lexer.token.kind == TokenKind::KwDefault) {
      bool is_default = (lexer.token.kind == TokenKind::KwDefault);
      next_token();

      CaseInfo ci;
      if (!is_default) {
        double val   = lexer.token.num_val;
        ci.cpool_idx = cpool_add(Value::float64(val));
        next_token();
      }
      expect(TokenKind::Colon);
      ci.body_lab = new_label();
      cases.push_back(ci);

      emit_label(ci.body_lab);
      while (lexer.token.kind != TokenKind::KwCase && lexer.token.kind != TokenKind::KwDefault && lexer.token.kind != TokenKind::RBrace) {
        parse_statement_or_decl(1);
      }
    } else {
      break;
    }
  }
  expect(TokenKind::RBrace);

  emit_jump(RegOp::JMP, label_end, 0);

  emit_label(label_chain);

  for (auto &ci : cases) {
    if (ci.cpool_idx < 0) {
      emit_jump(RegOp::JMP, ci.body_lab, 0);
    } else {
      int case_reg = alloc_temp();
      emit_iABx(RegOp::LOADK, static_cast<uint8_t>(case_reg), static_cast<uint16_t>(ci.cpool_idx));
      int cmp = alloc_temp();
      emit_iABC(RegOp::SEQ, static_cast<uint8_t>(cmp), static_cast<uint8_t>(expr.reg), static_cast<uint8_t>(case_reg));
      emit_jump(RegOp::IS_TRUE, ci.body_lab, static_cast<uint8_t>(cmp));
      free_temp();
      free_temp();
    }
  }
  emit_jump(RegOp::JMP, label_end, 0);

  emit_label(label_end);
  cur_func->pop_break();
  free_temp();
}

void RegParseState::parse_block() {
  expect(TokenKind::LBrace);
  if (lexer.token.kind != TokenKind::RBrace) {
    push_enter_scope();
    for (;;) {
      parse_statement_or_decl(1);
      if (lexer.token.kind == TokenKind::RBrace)
        break;
    }
    pop_leave_scope();
  }
  next_token();
}

void RegParseState::parse_expr_statement() {
  RegSlot result = parse_expr();
  if (lexer.token.kind == TokenKind::Semicolon) {
    next_token();
  } else if (lexer.token.kind == TokenKind::Eof || lexer.token.kind == TokenKind::RBrace || lexer.got_lf) {
    // ASI
  }
  if (cur_func->eval_ret_reg >= 0) {
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cur_func->eval_ret_reg), static_cast<uint8_t>(result.reg), 0);
  }
  free_temp();
}

void RegParseState::parse_if_statement() {
  next_token();
  push_enter_scope();

  expect(TokenKind::LParen);
  RegSlot cond = parse_expr();
  expect(TokenKind::RParen);

  int false_label = new_label();
  emit_jump(RegOp::IS_FALSE, false_label, static_cast<uint8_t>(cond.reg));
  free_temp();

  parse_statement();

  if (lexer.token.kind == TokenKind::KwElse) {
    int end_label = new_label();
    emit_jump(RegOp::JMP, end_label, 0);
    next_token();
    emit_label(false_label);
    parse_statement();
    emit_label(end_label);
  } else {
    emit_label(false_label);
  }

  pop_leave_scope();
}

void RegParseState::parse_return_statement() {
  next_token();

  for (auto &ti : try_stack_) {
    if (ti.finally_label >= 0) {
      cur_func->emit_jump(RegOp::GOSUB, ti.finally_label, 0);
    }
  }

  if (lexer.token.kind != TokenKind::Semicolon && lexer.token.kind != TokenKind::RBrace && !lexer.got_lf) {
    RegSlot result = parse_expr();
    emit_iABC(RegOp::RETURN, static_cast<uint8_t>(result.reg), 0, 0);
    free_temp();
  } else {
    emit_iABx(RegOp::RETURN0, 0, 0);
  }

  if (lexer.token.kind == TokenKind::Semicolon)
    next_token();
}

void RegParseState::parse_throw_statement() {
  next_token();
  if (lexer.got_lf)
    return;
  RegSlot result = parse_expr();
  emit_iABC(RegOp::THROW, static_cast<uint8_t>(result.reg), 0, 0);
  free_temp();
  if (lexer.token.kind == TokenKind::Semicolon)
    next_token();
}

void RegParseState::parse_while_statement() {
  BlockEnv be;
  int label_cont  = new_label();
  int label_break = new_label();

  cur_func->push_break(&be, pending_label, label_break, label_cont);

  next_token();
  emit_label(label_cont);

  expect(TokenKind::LParen);
  RegSlot cond = parse_expr();
  expect(TokenKind::RParen);

  emit_jump(RegOp::IS_FALSE, label_break, static_cast<uint8_t>(cond.reg));
  free_temp();

  parse_statement();
  emit_jump(RegOp::JMP, label_cont, 0);

  emit_label(label_break);
  cur_func->pop_break();
}

void RegParseState::parse_do_statement() {
  BlockEnv be;
  int label_cont  = new_label();
  int label_break = new_label();
  int label_body  = new_label();

  cur_func->push_break(&be, pending_label, label_break, label_cont);

  next_token();
  emit_label(label_body);

  parse_statement();
  expect(TokenKind::KwWhile);

  emit_label(label_cont);
  expect(TokenKind::LParen);
  RegSlot cond = parse_expr();
  expect(TokenKind::RParen);

  emit_jump(RegOp::IS_TRUE, label_body, static_cast<uint8_t>(cond.reg));
  free_temp();
  emit_label(label_break);

  cur_func->pop_break();

  if (lexer.token.kind == TokenKind::Semicolon)
    next_token();
}

void RegParseState::parse_for_statement() {
  next_token();
  expect(TokenKind::LParen);

  int block_scope_level = cur_func->scope_level;
  push_enter_scope();

  bool is_for_in = false;
  bool is_for_of = false;
  (void)is_for_of;

  TokenKind tok = lexer.token.kind;
  if (tok != TokenKind::Semicolon) {
    if (tok == TokenKind::KwVar || tok == TokenKind::KwLet || tok == TokenKind::KwConst) {
      next_token();
      parse_var_decls(tok);

      if (lexer.token.kind == TokenKind::KwIn) {
        is_for_in = true;
      } else if (lexer.token.kind == TokenKind::KwOf) {
        is_for_of = true;
      }

      if (!is_for_in && !is_for_of) {
        cur_func->close_scopes(cur_func->scope_level, block_scope_level);
      }
    } else {
      (void)parse_assign_expr();
      free_temp();

      if (lexer.token.kind == TokenKind::KwIn) {
        is_for_in = false;
      }
    }
  }

  if (is_for_in) {
    next_token(); // skip 'in'

    int key_var_idx = cur_func->var_count - 1;
    int key_reg     = cur_func->alloc.var(key_var_idx);

    RegSlot obj_slot = parse_expr();
    expect(TokenKind::RParen);

    int iter_reg = alloc_temp();
    int more_reg = alloc_temp();

    emit_iABC(RegOp::FOR_IN_START, static_cast<uint8_t>(iter_reg), static_cast<uint8_t>(obj_slot.reg), 0);
    free_temp();

    int loop_label = new_label();
    int end_label  = new_label();

    emit_jump(RegOp::JMP, loop_label, 0);
    int body_label = new_label();
    emit_label(body_label);

    parse_statement();

    emit_label(loop_label);
    emit_iABC(RegOp::FOR_IN_NEXT, static_cast<uint8_t>(key_reg), static_cast<uint8_t>(iter_reg), static_cast<uint8_t>(more_reg));
    emit_jump(RegOp::IS_TRUE, body_label, static_cast<uint8_t>(more_reg));
    emit_label(end_label);

    free_temp();
    free_temp();

    cur_func->close_scopes(cur_func->scope_level, block_scope_level);
    pop_leave_scope();
    return;
  }

  if (is_for_of) {
    next_token(); // skip 'of'

    int val_var_idx = cur_func->var_count - 1;
    int val_reg     = cur_func->alloc.var(val_var_idx);

    RegSlot iterable_slot = parse_expr();
    expect(TokenKind::RParen);

    int si_cpool = cpool_add(rt->atom_to_value(rt->well_known.symbol_iterator));

    // Pre-allocate all temps for the entire for-of loop.
    // This avoids LIFO free-order corruption.
    int iterator_reg   = alloc_temp();
    int call_base      = alloc_temp();
    int this_reg       = alloc_temp();
    int result_reg     = alloc_temp();
    int next_fn_reg    = alloc_temp();
    int next_this_reg  = alloc_temp();
    int done_reg       = alloc_temp();
    int value_reg      = alloc_temp();

    // Call iterable[Symbol.iterator]() with this=iterable
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(call_base), static_cast<uint8_t>(iterable_slot.reg), static_cast<uint8_t>(si_cpool));
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(this_reg), static_cast<uint8_t>(iterable_slot.reg), 0);
    emit_iABC(RegOp::CALL_M, static_cast<uint8_t>(iterator_reg), static_cast<uint8_t>(call_base), 0);

    int next_cpool  = cpool_add(rt->atom_to_value(rt->intern("next")));
    int done_cpool  = cpool_add(rt->atom_to_value(rt->intern("done")));
    int value_cpool = cpool_add(rt->atom_to_value(rt->intern("value")));

    int loop_label = new_label();
    int end_label  = new_label();
    int body_label = new_label();

    emit_jump(RegOp::JMP, loop_label, 0);
    emit_label(body_label);
    parse_statement();
    cur_func->close_scopes(cur_func->scope_level, block_scope_level);

    // Loop header: result = iterator.next() with this=iterator
    emit_label(loop_label);
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(next_fn_reg), static_cast<uint8_t>(iterator_reg), static_cast<uint8_t>(next_cpool));
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(next_this_reg), static_cast<uint8_t>(iterator_reg), 0);
    emit_iABC(RegOp::CALL_M, static_cast<uint8_t>(result_reg), static_cast<uint8_t>(next_fn_reg), 0);

    // Check result.done
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(done_reg), static_cast<uint8_t>(result_reg), static_cast<uint8_t>(done_cpool));
    emit_jump(RegOp::IS_TRUE, end_label, static_cast<uint8_t>(done_reg));

    // Move result.value → loop variable
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(value_reg), static_cast<uint8_t>(result_reg), static_cast<uint8_t>(value_cpool));
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(val_reg), static_cast<uint8_t>(value_reg), 0);
    emit_jump(RegOp::JMP, body_label, 0);

    emit_label(end_label);
    free_temp(); // value_reg
    free_temp(); // done_reg
    free_temp(); // next_this_reg
    free_temp(); // next_fn_reg
    free_temp(); // result_reg
    free_temp(); // this_reg
    free_temp(); // call_base
    free_temp(); // iterator_reg

    pop_leave_scope();
    return;
  }

  expect(TokenKind::Semicolon);

  int label_test  = new_label();
  int label_cont  = new_label();
  int label_body  = new_label();
  int label_break = new_label();

  BlockEnv be;
  cur_func->push_break(&be, pending_label, label_break, label_cont);

  if (lexer.token.kind == TokenKind::Semicolon) {
    label_test = label_body;
  } else {
    emit_label(label_test);
    RegSlot cond = parse_expr();
    emit_jump(RegOp::IS_FALSE, label_break, static_cast<uint8_t>(cond.reg));
    free_temp();
  }
  expect(TokenKind::Semicolon);

  int update_start = -1;
  if (lexer.token.kind == TokenKind::RParen) {
    be.label_cont = label_cont = label_test;
  } else {
    emit_jump(RegOp::JMP, label_body, 0);

    emit_label(label_cont);
    update_start = (int)cur_func->instructions.size();

    RegSlot upd = parse_expr();
    (void)upd;
    free_temp();

    emit_jump(RegOp::JMP, label_test, 0);
  }

  expect(TokenKind::RParen);

  int body_start = (int)cur_func->instructions.size();
  emit_label(label_body);
  parse_statement();

  cur_func->close_scopes(cur_func->scope_level, block_scope_level);

  if (update_start >= 0) {
    int update_size = body_start - update_start;

    if (update_size > 0) {
      cur_func->instructions.insert(cur_func->instructions.end(), cur_func->instructions.begin() + update_start,
                                    cur_func->instructions.begin() + body_start);

      for (int i = 0; i < update_size; i++)
        cur_func->instructions[static_cast<size_t>(update_start + i)] = Instruction::iABx(static_cast<uint8_t>(RegOp::NOP), 0, 0).raw;

      int offset = (int)cur_func->instructions.size() - body_start;
      for (size_t li = 0; li < cur_func->label_slots.size(); li++) {
        int p = cur_func->label_slots[li].pos;
        if (p >= update_start && p < body_start)
          cur_func->label_slots[li].pos = p + offset;
      }

      int new_dest = (int)cur_func->instructions.size() - update_size;
      for (auto &p : cur_func->patches) {
        if (p.instr_idx >= update_start && p.instr_idx < body_start)
          p.instr_idx = new_dest + (p.instr_idx - update_start);
      }
    }
  } else {
    emit_jump(RegOp::JMP, label_cont, 0);
  }

  emit_label(label_break);
  cur_func->pop_break();
  pop_leave_scope();
}

void RegParseState::parse_break_continue(bool is_cont) {
  next_token();

  Atom label_name = kAtomNull;
  if (lexer.token.kind == TokenKind::Identifier && !lexer.got_lf) {
    label_name = lexer.token.ident_atom;
    next_token();
  }

  FunctionDef *fd = cur_func;
  int scope       = fd->scope_level;
  BlockEnv *top   = fd->top_break;

  while (top) {
    fd->close_scopes(scope, top->scope_level);
    scope = top->scope_level;
    if (label_name != kAtomNull && top->label_name != label_name) {
      top = top->prev;
      continue;
    }

    if (is_cont && top->label_cont >= 0) {
      for (auto it = try_stack_.rbegin(); it != try_stack_.rend(); ++it) {
        if (it->finally_label >= 0) {
          fd->emit_jump(RegOp::GOSUB, it->finally_label, 0);
        }
      }
      fd->emit_jump(RegOp::JMP, top->label_cont, 0);
      goto done;
    }
    if (!is_cont && top->label_break >= 0) {
      for (auto it = try_stack_.rbegin(); it != try_stack_.rend(); ++it) {
        if (it->finally_label >= 0) {
          fd->emit_jump(RegOp::GOSUB, it->finally_label, 0);
        }
      }
      fd->emit_jump(RegOp::JMP, top->label_break, 0);
      goto done;
    }
    top = top->prev;
  }

done:
  if (lexer.token.kind == TokenKind::Semicolon)
    next_token();
}

// ─── Variable declarations ────────────────────────────────────────────────────

void RegParseState::parse_var_decls(TokenKind decl_tok) {
  for (;;) {
    // ── Array destructuring: var/let/const [a, b] = expr ───────────────
    if (lexer.token.kind == TokenKind::LBracket) {
      next_token(); // skip '['

      struct ArrBind {
        Atom name = kAtomNull;
        int reg = -1;
        bool is_nested_arr = false;
        bool is_nested_obj = false;
        int nested_start = -1;
        bool has_default = false;
        size_t def_start = 0;
        size_t def_end = 0;
      };
      std::vector<ArrBind> binds;

      while (lexer.token.kind != TokenKind::RBracket) {
        if (lexer.token.kind == TokenKind::Comma) {
          binds.push_back({kAtomNull, -1}); // elision
          next_token();
          continue;
        }
        // Nested array: [a, [b, c]]
        if (lexer.token.kind == TokenKind::LBracket) {
          next_token();
          // Count how many binds in the nested pattern
          ArrBind nb; nb.is_nested_arr = true; nb.nested_start = (int)binds.size();
          binds.push_back(nb);
          int nested_count = 0;
          while (lexer.token.kind != TokenKind::RBracket) {
            if (lexer.token.kind == TokenKind::Comma) { next_token(); continue; }
            if (lexer.token.kind != TokenKind::Identifier) return;
            Atom name = lexer.token.ident_atom; next_token();
            if (!js_define_var(name, decl_tok)) return;
            int idx = cur_func->find_var(name);
            binds.push_back({name, cur_func->alloc.var(idx < 0 ? 0 : static_cast<uint32_t>(idx))});
            nested_count++;
            if (lexer.token.kind == TokenKind::Comma) next_token();
          }
          next_token(); // skip ']'
          // Store the nested count so we know how many binds belong to this nest
          binds[static_cast<size_t>(nb.nested_start)].reg = nested_count;
          if (lexer.token.kind == TokenKind::Comma) next_token();
          continue;
        }
        if (lexer.token.kind != TokenKind::Identifier)
          return;
        Atom name = lexer.token.ident_atom;
        next_token();
        if (!js_define_var(name, decl_tok))
          return;
        int idx = cur_func->find_var(name);
        ArrBind bind;
        bind.name = name;
        bind.reg = cur_func->alloc.var(idx < 0 ? 0 : static_cast<uint32_t>(idx));

        // Capture default value source range for lazy re-parsing
        if (lexer.token.kind == TokenKind::EqAssign) {
          size_t def_s = lexer.buf_pos(); // '=' already consumed by earlier next_token
          // Skip tokens of the default expression
          int depth = 0;
          for (;;) {
            TokenKind k = lexer.token.kind;
            if (k == TokenKind::LParen || k == TokenKind::LBracket || k == TokenKind::LBrace) depth++;
            else if (k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace) {
              if (depth == 0) break;
              depth--;
            } else if (depth == 0 && (k == TokenKind::Comma || k == TokenKind::RBracket || k == TokenKind::RBrace)) break;
            if (k == TokenKind::Eof) break;
            next_token();
          }
          bind.has_default = true;
          bind.def_start = def_s;
          bind.def_end   = lexer.buf_pos();
        }
        binds.push_back(bind);

        if (lexer.token.kind != TokenKind::RBracket && lexer.token.kind != TokenKind::Comma)
          return;
        if (lexer.token.kind == TokenKind::Comma)
          next_token();
      }
      next_token(); // skip ']'

      if (lexer.token.kind != TokenKind::EqAssign) {
        if (decl_tok == TokenKind::KwConst) return;
      } else {
        next_token();
        RegSlot rhs = parse_assign_expr();
        int arr_pos = 0; // track actual array index
        for (size_t i = 0; i < binds.size(); ++i) {
          auto &b = binds[i];

          // Nested array: extract element, then recurse for inner binds
          if (b.is_nested_arr) {
            int nested_count = b.reg;
            int elem_reg = alloc_temp(); // before idx_reg so free_temp frees idx_reg
            int idx_reg = alloc_temp();
            emit_iAsBx(RegOp::LOADINT, static_cast<uint8_t>(idx_reg), static_cast<int16_t>(arr_pos));
            emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(elem_reg), static_cast<uint8_t>(rhs.reg), static_cast<uint8_t>(idx_reg));
            free_temp(); // idx_reg
            for (int j = 0; j < nested_count; j++) {
              i++;
              auto &inner = binds[i];
              if (inner.name == kAtomNull) continue;
              int inner_idx = alloc_temp();
              emit_iAsBx(RegOp::LOADINT, static_cast<uint8_t>(inner_idx), static_cast<int16_t>(j));
              int inner_elem = alloc_temp();
              emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(inner_elem), static_cast<uint8_t>(elem_reg), static_cast<uint8_t>(inner_idx));
              free_temp(); // inner_idx
              emit_iABC(RegOp::MOVE, static_cast<uint8_t>(inner.reg), static_cast<uint8_t>(inner_elem), 0);
              free_temp(); // inner_elem
            }
            free_temp(); // elem_reg
            arr_pos++;
            continue;
          }

          if (b.name == kAtomNull) { arr_pos++; continue; } // elision

          // Regular binding
          int idx_reg = alloc_temp();
          emit_iAsBx(RegOp::LOADINT, static_cast<uint8_t>(idx_reg), static_cast<int16_t>(arr_pos));
          int elem_reg = alloc_temp();
          emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(elem_reg), static_cast<uint8_t>(rhs.reg), static_cast<uint8_t>(idx_reg));
          free_temp(); // idx_reg

          // Default value: re-parse expression from saved source range
          if (b.has_default) {
            int skip_label = new_label();
            emit_jump(RegOp::IS_TRUE, skip_label, {elem_reg}); // skip if value is truthy
            Lexer saved = lexer;
            lexer.reset(lexer.buf_start + b.def_start, b.def_end - b.def_start);
            lexer.next_token();
            RegSlot def = parse_assign_expr();
            emit_iABC(RegOp::MOVE, static_cast<uint8_t>(elem_reg), static_cast<uint8_t>(def.reg), 0);
            free_temp(); // def
            lexer = saved;
            emit_label(skip_label);
          }

          emit_iABC(RegOp::MOVE, static_cast<uint8_t>(b.reg), static_cast<uint8_t>(elem_reg), 0);
          free_temp(); // elem_reg
          arr_pos++;
        }
        free_temp(); // rhs
      }

      if (lexer.token.kind != TokenKind::Comma)
        return;
      next_token();
      continue;
    }

    // ── Object destructuring: var/let/const {a, b: c} = expr ───────────
    if (lexer.token.kind == TokenKind::LBrace) {
      next_token(); // skip '{'

      struct ObjBind { Atom prop; Atom var_name; int reg = -1; bool has_default = false; size_t def_start = 0; size_t def_end = 0; };
      std::vector<ObjBind> binds;

      while (lexer.token.kind != TokenKind::RBrace) {
        if (lexer.token.kind != TokenKind::Identifier)
          return;
        Atom prop = lexer.token.ident_atom;
        next_token();

        Atom var_name = prop; // shorthand: {x}
        if (lexer.token.kind == TokenKind::Colon) {
          next_token();
          if (lexer.token.kind != TokenKind::Identifier)
            return;
          var_name = lexer.token.ident_atom;
          next_token();
        }
        if (!js_define_var(var_name, decl_tok))
          return;
        int idx = cur_func->find_var(var_name);
        ObjBind bind;
        bind.prop = prop;
        bind.var_name = var_name;
        bind.reg = cur_func->alloc.var(idx < 0 ? 0 : static_cast<uint32_t>(idx));

        if (lexer.token.kind == TokenKind::EqAssign) {
          size_t def_s = lexer.buf_pos(); // '=' already consumed by earlier next_token
          int depth = 0;
          for (;;) {
            TokenKind k = lexer.token.kind;
            if (k == TokenKind::LParen || k == TokenKind::LBracket || k == TokenKind::LBrace) depth++;
            else if (k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace) {
              if (depth == 0) break;
              depth--;
            } else if (depth == 0 && (k == TokenKind::Comma || k == TokenKind::RBrace)) break;
            if (k == TokenKind::Eof) break;
            next_token();
          }
          bind.has_default = true;
          bind.def_start = def_s;
          bind.def_end   = lexer.buf_pos();
        }
        binds.push_back(bind);

        if (lexer.token.kind != TokenKind::RBrace && lexer.token.kind != TokenKind::Comma)
          return;
        if (lexer.token.kind == TokenKind::Comma)
          next_token();
      }
      next_token(); // skip '}'

      if (lexer.token.kind != TokenKind::EqAssign) {
        if (decl_tok == TokenKind::KwConst) return;
      } else {
        next_token();
        RegSlot rhs = parse_assign_expr();
        for (auto &b : binds) {
          int ci = cpool_add(rt->atom_to_value(b.prop));
          int elem_reg = alloc_temp();
          emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(elem_reg), static_cast<uint8_t>(rhs.reg), static_cast<uint8_t>(ci));

          if (b.has_default) {
            int skip_label = new_label();
            emit_jump(RegOp::IS_TRUE, skip_label, {elem_reg});
            Lexer saved = lexer;
            lexer.reset(lexer.buf_start + b.def_start, b.def_end - b.def_start);
            lexer.next_token();
            RegSlot def = parse_assign_expr();
            emit_iABC(RegOp::MOVE, static_cast<uint8_t>(elem_reg), static_cast<uint8_t>(def.reg), 0);
            free_temp(); // def
            lexer = saved;
            emit_label(skip_label);
          }

          emit_iABC(RegOp::MOVE, static_cast<uint8_t>(b.reg), static_cast<uint8_t>(elem_reg), 0);
          free_temp(); // elem_reg
        }
        free_temp();
      }

      if (lexer.token.kind != TokenKind::Comma)
        return;
      next_token();
      continue;
    }

    // ── Simple identifier ───────────────────────────────────────────────
    if (lexer.token.kind != TokenKind::Identifier)
      return;
    Atom name = lexer.token.ident_atom;
    next_token();

    if (!js_define_var(name, decl_tok))
      return;

    if (lexer.token.kind == TokenKind::EqAssign) {
      next_token();
      LValue lv;
      bool found = false;
      if (decl_tok != TokenKind::KwVar) {
        VarDef *vd = cur_func->find_scope_var(name, cur_func->scope_level);
        if (vd) {
          int idx = 0;
          for (int i = 0; i < cur_func->var_count; i++) {
            if (&cur_func->vars[static_cast<size_t>(i)] == vd) {
              idx = i;
              break;
            }
          }
          lv.kind    = LValue::LOCAL;
          lv.var_idx = idx;
          lv.prop    = name;
          found      = true;
        }
      } else {
        int vi = cur_func->find_var(name);
        if (vi >= 0 && vi < cur_func->var_count) {
          lv.kind    = LValue::LOCAL;
          lv.var_idx = vi;
          lv.prop    = name;
          found      = true;
        } else {
          lv.kind = LValue::GLOBAL;
          lv.prop = name;
          found   = true;
        }
      }
      RegSlot rhs = parse_assign_expr();
      if (found)
        emit_lvalue_store(lv, rhs);
      free_temp();
    } else if (decl_tok == TokenKind::KwConst) {
      return;
    }

    if (lexer.token.kind != TokenKind::Comma)
      break;
    next_token();
  }
}

// ─── Function parsing ───────────────────────────────────────────────────────

FunctionDef *RegParseState::parse_function_decl(Atom name, bool is_expr, FunctionKind func_kind) {
  FunctionDef *parent_fd = cur_func;
  auto *fd               = new FunctionDef(rt);
  fd->parent             = parent_fd;
  fd->func_name          = name;
  fd->func_kind          = func_kind;
  fd->is_func_expr       = is_expr;
  fd->has_prototype      = true;
  fd->has_this_binding   = true;
  fd->arguments_allowed  = true;
  if (parent_fd) {
    fd->js_mode            = parent_fd->js_mode;
    fd->parent_scope_level = parent_fd->scope_level;
  }

  cur_func = fd;

  expect(TokenKind::LParen);
  fd->push_scope();
  fd->body_scope = fd->scope_level;

  if (lexer.token.kind != TokenKind::RParen) {
    for (;;) {
      if (lexer.token.kind != TokenKind::Identifier)
        goto fail;
      Atom arg_name = lexer.token.ident_atom;
      next_token();

      int idx = fd->add_arg(arg_name);
      if (idx < 0)
        goto fail;

      if (lexer.token.kind == TokenKind::RParen)
        break;
      if (lexer.token.kind != TokenKind::Comma)
        goto fail;
      next_token();
    }
  }
  expect(TokenKind::RParen);

  fd->alloc.init(fd->arg_count, fd->var_count);

  expect(TokenKind::LBrace);

  fd->in_function_body = true;

  while (lexer.token.kind != TokenKind::RBrace) {
    parse_statement_or_decl(1);
  }
  next_token();

  emit_iABx(RegOp::RETURN0, 0, 0);

  fd->pop_scope();

  {
    int cpool_idx        = parent_fd->cpool_add(Value::bytecode(nullptr));
    fd->parent_cpool_idx = cpool_idx;
  }

  parent_fd->children.push_back(fd);

  cur_func = parent_fd;

  return fd;

fail:
  cur_func = parent_fd;
  delete fd;
  return nullptr;
}

// ─── Compilation entry ──────────────────────────────────────────────────────

bool RegParseState::compile() {
  auto *fd              = new FunctionDef(rt);
  fd->is_eval           = true;
  fd->is_global_var     = true;
  fd->has_this_binding  = true;
  fd->arguments_allowed = true;
  fd->in_function_body  = true;

  cur_func = fd;
  lexer.next_token();

  fd->push_scope();
  fd->body_scope = fd->scope_level;

  fd->alloc.init(0, 0);

  fd->eval_ret_reg = 0;

  while (lexer.token.kind != TokenKind::Eof) {
    parse_statement_or_decl(1);
  }

  emit_iABC(RegOp::RETURN, static_cast<uint8_t>(fd->eval_ret_reg), 0, 0);
  fd->pop_scope();

  return true;
}

// ─── Lowering ───────────────────────────────────────────────────────────────
} // namespace qjsp
