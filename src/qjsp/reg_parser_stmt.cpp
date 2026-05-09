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
  if (lexer.token.type == TOK_IDENT &&
      !lexer.token.ident_is_reserved &&
      peek_token(true) == ':') {
    Atom label = lexer.token.ident_atom;
    next_token(); // skip ident
    next_token(); // skip ':'

    bool is_loop = (lexer.token.type == TOK_FOR ||
                    lexer.token.type == TOK_WHILE ||
                    lexer.token.type == TOK_DO);

    if (is_loop) {
      // For labeled loops, pass label name to the loop via pending_label
      pending_label = label;
      if (lexer.token.type == TOK_FOR)         parse_for_statement();
      else if (lexer.token.type == TOK_WHILE)  parse_while_statement();
      else                                     parse_do_statement();
      pending_label = kAtomNull;
    } else {
      // Labeled regular statement: create BlockEnv with label_break only
      BlockEnv be;
      int lbreak = new_label();
      cur_func->push_break(&be, label, lbreak, -1);
      parse_statement();
      emit_label(lbreak);
      cur_func->pop_break();
    }
    return;
  }

  int tok = lexer.token.type;

  switch (tok) {
  case '{':
    parse_block();
    return;
  case TOK_IF:
    parse_if_statement();
    return;
  case TOK_RETURN:
    parse_return_statement();
    return;
  case TOK_THROW:
    parse_throw_statement();
    return;
  case TOK_WHILE:
    parse_while_statement();
    return;
  case TOK_FOR:
    parse_for_statement();
    return;
  case TOK_DO:
    parse_do_statement();
    return;
  case TOK_BREAK:
    parse_break_continue(false);
    return;
  case TOK_CONTINUE:
    parse_break_continue(true);
    return;
  case TOK_SWITCH:
    parse_switch_statement();
    return;
  case TOK_TRY: {
    parse_try_statement();
    return;
  }

  case TOK_LET:
  case TOK_CONST:
    if (!(decl_mask & 1)) {
      parse_expr_statement();
      return;
    }
  case TOK_VAR: {
    next_token();
    parse_var_decls(tok);
    // Semicolon
  if (lexer.token.type == ';')
    next_token();
  return;
}

  case TOK_FUNCTION: {
    next_token();
    Atom name = kAtomNull;
    if (lexer.token.type == TOK_IDENT) {
      name = lexer.token.ident_atom;
      next_token();
    }
    FunctionDef *fd = parse_function_decl(name, false, FunctionKind::normal);
    if (!fd)
      return;

    // Store the function object into a local variable
    if (name != kAtomNull) {
      int var_idx = cur_func->find_var(name);
      if (var_idx < 0) {
        js_define_var(name, TOK_VAR);
        var_idx = cur_func->find_var(name);
      }
      int r = alloc_temp();
      emit_iABx(RegOp::FCLOSURE, static_cast<uint8_t>(r), static_cast<uint16_t>(fd->parent_cpool_idx));
      emit_lvalue_store({LValue::LOCAL, -1, name, -1, var_idx}, {r});
      free_temp();
    }
    return;
  }

  case ';':
    next_token();
    return;

  default:
    parse_expr_statement();
    return;
  }
}

void RegParseState::parse_try_statement() {
  next_token(); // skip 'try'

  constexpr int kExcReg = 200; // reserved register for exception value

  int exc_reg        = kExcReg;
  int finalize_label = new_label(); // finally subroutine label
  int catch_label    = new_label();
  int after_label    = new_label();

  // Push try info (finally_label may stay unused if no finally)
  TryInfo ti;
  ti.exc_reg       = exc_reg;
  ti.scope_level   = cur_func->scope_level;
  ti.finally_label = finalize_label;
  try_stack_.push_back(ti);

  // CATCH — push catch frame
  cur_func->emit_jump(RegOp::CATCH, catch_label, static_cast<uint8_t>(exc_reg));

  // Parse try body
  expect('{');
  while (lexer.token.type != '}') {
    parse_statement_or_decl(1);
  }
  next_token();

  // Normal exit
  bool has_finally = false;
  if (lexer.token.type == TOK_FINALLY) {
    has_finally = true;
    cur_func->emit_jump(RegOp::GOSUB, finalize_label, 0);
  }
  emit_iABx(RegOp::UNCATCH, 0, 0);
  int skip_catch_label = new_label();
  emit_jump(RegOp::JMP, skip_catch_label, 0);

  // Catch block
  emit_label(catch_label);

  if (lexer.token.type == TOK_CATCH) {
    next_token();
    if (lexer.token.type == '(') {
      next_token();
      if (lexer.token.type == TOK_IDENT) {
        Atom catch_name = lexer.token.ident_atom;
        next_token();
        expect(')');
        push_enter_scope();
        js_define_var(catch_name, TOK_LET);
        LValue catch_lv;
        for (int i = 0; i < cur_func->var_count; i++) {
          if (cur_func->vars[static_cast<size_t>(i)].var_name == catch_name &&
              cur_func->vars[static_cast<size_t>(i)].scope_level == cur_func->scope_level) {
            catch_lv.kind = LValue::LOCAL;
            catch_lv.var_idx = i;
            break;
          }
        }
        emit_lvalue_store(catch_lv, {exc_reg});
      } else if (lexer.token.type == ')') {
        next_token();
      }
      expect('{');
      while (lexer.token.type != '}') {
        parse_statement_or_decl(1);
      }
      next_token();
      pop_leave_scope();
    }
    // If there's a finally after catch, emit GOSUB before UNCATCH
    if (lexer.token.type == TOK_FINALLY) {
      has_finally = true;
      cur_func->emit_jump(RegOp::GOSUB, finalize_label, 0);
    }
  } else if (has_finally) {
    // No catch, but has finally: call finally then re-throw
    cur_func->emit_jump(RegOp::GOSUB, finalize_label, 0);
    emit_iABC(RegOp::THROW, static_cast<uint8_t>(exc_reg), 0, 0);
  }
  emit_iABx(RegOp::UNCATCH, 0, 0);

  // Both paths converge here — skip the finally subroutine
  emit_label(skip_catch_label);
  emit_jump(RegOp::JMP, after_label, 0);

  // Pop try info
  try_stack_.pop_back();

  // Finally block — emitted as subroutine
  if (has_finally) {
    next_token(); // skip 'finally'
    emit_label(finalize_label);
    expect('{');
    while (lexer.token.type != '}') {
      parse_statement_or_decl(1);
    }
    next_token();
    emit_iABx(RegOp::RET, 0, 0);
  }

  emit_label(after_label);
  cur_func->alloc.ensure_max(exc_reg + 1);
}

void RegParseState::parse_switch_statement() {
  next_token(); // skip 'switch'

  expect('(');
  RegSlot expr = parse_expr();
  expect(')');
  expect('{');

  int label_end   = new_label();
  int label_chain = new_label(); // comparison chain start

  // Block env for break
  BlockEnv be;
  cur_func->push_break(&be, kAtomNull, label_end, -1);

  // JMP over bodies to comparison chain
  emit_jump(RegOp::JMP, label_chain, 0);

  // Case info
  struct CaseInfo {
    int body_lab;
    int cpool_idx = -1; // cpool index for case value (if not default)
  };
  std::vector<CaseInfo> cases;

  while (lexer.token.type != '}') {
    if (lexer.token.type == TOK_CASE || lexer.token.type == TOK_DEFAULT) {
      bool is_default = (lexer.token.type == TOK_DEFAULT);
      next_token();

      CaseInfo ci;
      if (!is_default) {
        // Parse case value: capture the literal value into cpool
        double val = lexer.token.num_val;
        ci.cpool_idx = cpool_add(Value::float64(val));
        next_token(); // skip the number
      }
      expect(':');
      ci.body_lab = new_label();
      cases.push_back(ci);

      emit_label(ci.body_lab);
      while (lexer.token.type != TOK_CASE &&
             lexer.token.type != TOK_DEFAULT &&
             lexer.token.type != '}') {
        parse_statement_or_decl(1);
      }
    } else {
      break;
    }
  }
  expect('}');

  emit_jump(RegOp::JMP, label_end, 0);

  // ── Comparison chain ──────────────────────────────────────────────────
  emit_label(label_chain);

  for (auto &ci : cases) {
    if (ci.cpool_idx < 0) {
      // default — jump to its body
      emit_jump(RegOp::JMP, ci.body_lab, 0);
    } else {
      // Load case value from cpool, then compare
      int case_reg = alloc_temp();
      emit_iABx(RegOp::LOADK, static_cast<uint8_t>(case_reg),
                static_cast<uint16_t>(ci.cpool_idx));
      int cmp = alloc_temp();
      emit_iABC(RegOp::SEQ, static_cast<uint8_t>(cmp),
                static_cast<uint8_t>(expr.reg),
                static_cast<uint8_t>(case_reg));
      emit_jump(RegOp::IS_TRUE, ci.body_lab, static_cast<uint8_t>(cmp));
      free_temp(); // cmp
      free_temp(); // case_reg
    }
  }
  emit_jump(RegOp::JMP, label_end, 0);

  emit_label(label_end);
  cur_func->pop_break();
  free_temp(); // free expr
}

void RegParseState::parse_block() {
  expect('{');
  if (lexer.token.type != '}') {
    push_enter_scope();
    for (;;) {
      parse_statement_or_decl(1);
      if (lexer.token.type == '}')
        break;
    }
    pop_leave_scope();
  }
  next_token();
}

void RegParseState::parse_expr_statement() {
  RegSlot result = parse_expr();
  // Semicolon
  if (lexer.token.type == ';') {
    next_token();
  } else if (lexer.token.type == TOK_EOF || lexer.token.type == '}' || lexer.got_lf) {
    // ASI
  }
  // Store result to eval_ret for implicit return
  if (cur_func->eval_ret_reg >= 0) {
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cur_func->eval_ret_reg), static_cast<uint8_t>(result.reg), 0);
  }
  free_temp();
}

void RegParseState::parse_if_statement() {
  next_token();
  push_enter_scope();

  expect('(');
  RegSlot cond = parse_expr();
  expect(')');

  int false_label = new_label();
  emit_jump(RegOp::IS_FALSE, false_label, static_cast<uint8_t>(cond.reg));
  free_temp();

  parse_statement();

  if (lexer.token.type == TOK_ELSE) {
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

  // Emit GOSUB to any active finally blocks before returning
  for (auto &ti : try_stack_) {
    if (ti.finally_label >= 0) {
      cur_func->emit_jump(RegOp::GOSUB, ti.finally_label, 0);
    }
  }

  if (lexer.token.type != ';' && lexer.token.type != '}' && !lexer.got_lf) {
    RegSlot result = parse_expr();
    emit_iABC(RegOp::RETURN, static_cast<uint8_t>(result.reg), 0, 0);
    free_temp();
  } else {
    emit_iABx(RegOp::RETURN0, 0, 0);
  }

  if (lexer.token.type == ';')
    next_token();
}

void RegParseState::parse_throw_statement() {
  next_token();
  if (lexer.got_lf)
    return;
  RegSlot result = parse_expr();
  emit_iABC(RegOp::THROW, static_cast<uint8_t>(result.reg), 0, 0);
  free_temp();
  if (lexer.token.type == ';')
    next_token();
}

void RegParseState::parse_while_statement() {
  BlockEnv be;
  int label_cont  = new_label();
  int label_break = new_label();

  cur_func->push_break(&be, pending_label, label_break, label_cont);

  next_token();
  emit_label(label_cont);

  expect('(');
  RegSlot cond = parse_expr();
  expect(')');

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
  expect(TOK_WHILE);

  emit_label(label_cont);
  expect('(');
  RegSlot cond = parse_expr();
  expect(')');

  emit_jump(RegOp::IS_TRUE, label_body, static_cast<uint8_t>(cond.reg));
  free_temp();
  emit_label(label_break);

  cur_func->pop_break();

  if (lexer.token.type == ';')
    next_token();
}

void RegParseState::parse_for_statement() {
  next_token();
  expect('(');

  int block_scope_level = cur_func->scope_level;
  push_enter_scope();

  // ── INIT ──
  bool is_for_in = false;
  bool is_for_of = false; (void)is_for_of;

  int tok = lexer.token.type;
  if (tok != ';') {
    if (tok == TOK_VAR || tok == TOK_LET || tok == TOK_CONST) {
      next_token();
      parse_var_decls(tok);

      // Check if next token is 'in' or 'of'
      if (lexer.token.type == TOK_IN) {
        is_for_in = true;
      } else if (lexer.token.type == TOK_OF) {
        is_for_of = true;
      }

      if (!is_for_in && !is_for_of) {
        cur_func->close_scopes(cur_func->scope_level, block_scope_level);
      }
    } else {
      (void)parse_assign_expr2(0);
      free_temp();

      if (lexer.token.type == TOK_IN) {
        // for (expr in ...) — not supported yet, skip
        is_for_in = false;
      }
    }
  }

  if (is_for_in) {
    // Continue parsing: skip 'in', parse object expr
    next_token(); // skip 'in'

    // Find the loop variable (last declared var)
    int key_var_idx = cur_func->var_count - 1;
    int key_reg     = cur_func->alloc.var(key_var_idx);

    // Parse the object expression
    RegSlot obj_slot = parse_expr();
    expect(')');

    int iter_reg = alloc_temp();
    int more_reg = alloc_temp();

    emit_iABC(RegOp::FOR_IN_START, static_cast<uint8_t>(iter_reg), static_cast<uint8_t>(obj_slot.reg), 0);
    free_temp(); // free obj_slot

    // Labels
    int loop_label = new_label();
    int end_label  = new_label();

    emit_jump(RegOp::JMP, loop_label, 0);
    int body_label = new_label();
    emit_label(body_label);

    // Parse body
    parse_statement();

    // Loop back
    emit_label(loop_label);
    emit_iABC(RegOp::FOR_IN_NEXT, static_cast<uint8_t>(key_reg), static_cast<uint8_t>(iter_reg), static_cast<uint8_t>(more_reg));
    emit_jump(RegOp::IS_TRUE, body_label, static_cast<uint8_t>(more_reg));
    emit_label(end_label);

    free_temp(); // free more_reg
    free_temp(); // free iter_reg

    cur_func->close_scopes(cur_func->scope_level, block_scope_level);
    pop_leave_scope();
    return;
  }

  expect(';'); // consume the semicolon after init (non-for-in path)

  // ── Labels ──
  int label_test  = new_label();
  int label_cont  = new_label();
  int label_body  = new_label();
  int label_break = new_label();

  BlockEnv be;
  cur_func->push_break(&be, pending_label, label_break, label_cont);

  // ── COND ──
  if (lexer.token.type == ';') {
    label_test = label_body;
  } else {
    emit_label(label_test);
    RegSlot cond = parse_expr();
    emit_jump(RegOp::IS_FALSE, label_break, static_cast<uint8_t>(cond.reg));
    free_temp();
  }
  expect(';');

  // ── UPDATE ──
  int update_start = -1;
  if (lexer.token.type == ')') {
    be.label_cont = label_cont = label_test;
  } else {
    emit_jump(RegOp::JMP, label_body, 0);

    emit_label(label_cont);
    update_start = (int)cur_func->instructions.size();

    RegSlot upd = parse_expr();
    (void)upd;
    free_temp();

    if (label_test != label_body)
      emit_jump(RegOp::JMP, label_test, 0);
  }

  expect(')');

  // ── BODY ──
  int body_start = (int)cur_func->instructions.size();
  emit_label(label_body);
  parse_statement();

  cur_func->close_scopes(cur_func->scope_level, block_scope_level);

  // ── Move update code after body ──
  if (update_start >= 0) {
    int update_size = body_start - update_start;

    // Copy update instructions to end
    if (update_size > 0) {
      cur_func->instructions.insert(cur_func->instructions.end(), cur_func->instructions.begin() + update_start,
                                    cur_func->instructions.begin() + body_start);

      // Fill original positions with NOP
      for (int i = 0; i < update_size; i++)
        cur_func->instructions[static_cast<size_t>(update_start + i)] = Instruction::iABx(static_cast<uint8_t>(RegOp::NOP), 0, 0).raw;

      // Relocate labels that were in the moved range
      int offset = (int)cur_func->instructions.size() - body_start;
      for (size_t li = 0; li < cur_func->label_slots.size(); li++) {
        int p = cur_func->label_slots[li].pos;
        if (p >= update_start && p < body_start)
          cur_func->label_slots[li].pos = p + offset;
      }

      // Relocate patches that reference moved instructions
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

  // Check for named label: break foo / continue foo
  Atom label_name = kAtomNull;
  if (lexer.token.type == TOK_IDENT && !lexer.got_lf) {
    label_name = lexer.token.ident_atom;
    next_token();
  }

  FunctionDef *fd = cur_func;
  int scope       = fd->scope_level;
  BlockEnv *top   = fd->top_break;

  while (top) {
    fd->close_scopes(scope, top->scope_level);
    scope = top->scope_level;
    // Check label match
    if (label_name != kAtomNull && top->label_name != label_name) {
      top = top->prev;
      continue;
    }

    if (is_cont && top->label_cont >= 0) {
      // Emit GOSUB for finally blocks we're exiting (reverse: inner first)
      for (auto it = try_stack_.rbegin(); it != try_stack_.rend(); ++it) {
        if (it->finally_label >= 0) {
          fd->emit_jump(RegOp::GOSUB, it->finally_label, 0);
        }
      }
      fd->emit_jump(RegOp::JMP, top->label_cont, 0);
      goto done;
    }
    if (!is_cont && top->label_break >= 0) {
      // Emit GOSUB for finally blocks we're exiting (reverse: inner first)
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
  if (lexer.token.type == ';')
    next_token();
}

void RegParseState::parse_var_decls(int decl_tok) {
  for (;;) {
    if (lexer.token.type != TOK_IDENT)
      return;
    Atom name = lexer.token.ident_atom;
    next_token();

    if (!js_define_var(name, decl_tok))
      return;

    if (lexer.token.type == '=') {
      next_token();
      // Find the variable we just defined
      LValue lv;
      bool found = false;
      if (decl_tok != TOK_VAR) {
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
      RegSlot rhs = parse_assign_expr2(PF_IN_ACCEPTED);
      if (found)
        emit_lvalue_store(lv, rhs);
      free_temp();
    } else if (decl_tok == TOK_CONST) {
      return; // const must have initializer
    }

    if (lexer.token.type != ',')
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

  // Parse parameters: (a, b, c)
  expect('(');
  fd->push_scope();
  fd->body_scope = fd->scope_level;

  if (lexer.token.type != ')') {
    for (;;) {
      if (lexer.token.type != TOK_IDENT)
        goto fail;
      Atom arg_name = lexer.token.ident_atom;
      next_token();

      int idx = fd->add_arg(arg_name);
      if (idx < 0)
        goto fail;

      if (lexer.token.type == ')')
        break;
      if (lexer.token.type != ',')
        goto fail;
      next_token();
    }
  }
  expect(')');

  // Initialize register allocator
  fd->alloc.init(fd->arg_count, fd->var_count);

  // Parse body
  expect('{');

  fd->in_function_body = true;

  while (lexer.token.type != '}') {
    parse_statement_or_decl(1 /* DECL_MASK_ALL */);
  }
  next_token();

  emit_iABx(RegOp::RETURN0, 0, 0);

  fd->pop_scope();

  // Add to parent's cpool (placeholder)
  {
    int cpool_idx        = parent_fd->cpool_add(Value::bytecode(nullptr));
    fd->parent_cpool_idx = cpool_idx;
  }

  // Track child
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

  // Initialize reg alloc: no args, no vars yet
  fd->alloc.init(0, 0);

  // Use R[0] (this slot, unused in top-level eval) as implicit return register
  fd->eval_ret_reg = 0; // R[0] = this (unused) → repurpose as eval_ret

  // Parse statements until EOF
  while (lexer.token.type != TOK_EOF) {
    parse_statement_or_decl(1 /* DECL_MASK_ALL */);
  }

  emit_iABC(RegOp::RETURN, static_cast<uint8_t>(fd->eval_ret_reg), 0, 0);
  fd->pop_scope();

  return true;
}

// ─── Lowering ───────────────────────────────────────────────────────────────
} // namespace qjsp
