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
//  Expression parser
// ═══════════════════════════════════════════════════════════════════════════

RegSlot RegParseState::parse_primary() {
  int tok = lexer.token.type;

  switch (tok) {
  case TOK_NULL: {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADNULL, static_cast<uint8_t>(r), 0);
    next_token();
    return {r};
  }
  case TOK_FALSE: {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADFALSE, static_cast<uint8_t>(r), 0);
    next_token();
    return {r};
  }
  case TOK_TRUE: {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADTRUE, static_cast<uint8_t>(r), 0);
    next_token();
    return {r};
  }
  case TOK_NUMBER: {
    int r      = alloc_temp();
    double val = lexer.token.u.num.val;
    int32_t iv = static_cast<int32_t>(val);
    if (val == static_cast<double>(iv) && iv >= -32768 && iv <= 32767) {
      emit_iAsBx(RegOp::LOADINT, static_cast<uint8_t>(r), static_cast<int16_t>(iv));
    } else {
      int ci = cpool_add(Value::float64(val));
      emit_iABx(RegOp::LOADK, static_cast<uint8_t>(r), static_cast<uint16_t>(ci));
    }
    next_token();
    return {r};
  }
  case TOK_STRING: {
    auto *s = String::create(std::string_view{lexer.token.u.str.str, lexer.token.u.str.len});
    int ci  = cpool_add(Value::string(s));
    int r   = alloc_temp();
    emit_iABx(RegOp::LOADK, static_cast<uint8_t>(r), static_cast<uint16_t>(ci));
    next_token();
    return {r};
  }
  case TOK_IDENT: {
    Atom atom = lexer.token.u.ident.atom;
    next_token();

    // Check all vars (any scope level — let/const are scoped)
    for (int i = 0; i < cur_func->var_count; i++) {
      if (cur_func->vars[static_cast<size_t>(i)].var_name == atom) {
        return {cur_func->vars[static_cast<size_t>(i)].reg_index};
      }
    }
    // Check args
    for (int i = 0; i < cur_func->arg_count; i++) {
      if (cur_func->args[static_cast<size_t>(i)].var_name == atom) {
        return {cur_func->args[static_cast<size_t>(i)].reg_index};
      }
    }
    // Check enclosing scopes (closure variable)
    int upval = cur_func->resolve_upval(atom);
    if (upval >= 0) {
      return emit_upval_read(upval);
    }
    // Global: load from global object
    int r  = alloc_temp();
    int ci = cpool_add(Value::string(rt->atom_to_string(atom)));
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(r), static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
    return {r};
  }
  case '(': {
    next_token();
    RegSlot result = parse_expr();
    expect(')');
    return result;
  }
  case TOK_FUNCTION: {
    next_token();
    FunctionDef *fd = parse_function_decl(kAtomNull, true, FunctionKind::normal);
    if (!fd) {
      int r = alloc_temp();
      emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(r), 0);
      return {r};
    }
    int r = alloc_temp();
    emit_iABx(RegOp::FCLOSURE, static_cast<uint8_t>(r), static_cast<uint16_t>(fd->parent_cpool_idx));
    return {r};
  }
  case '{':
    return parse_object_literal();
  case '[':
    return parse_array_literal();
  default:
    return {alloc_temp()}; // error: return a dummy reg
  }
}

// ─── Object literal ─────────────────────────────────────────────────────

RegSlot RegParseState::parse_object_literal() {
  next_token(); // skip '{'

  int obj_reg = alloc_temp();
  emit_iABx(RegOp::NEWOBJ, static_cast<uint8_t>(obj_reg), 0);

  while (lexer.token.type != '}') {
    Atom name     = kAtomNull;
    bool computed = false;

    // Spread: { ...expr }
    if (lexer.token.type == TOK_ELLIPSIS) {
      next_token();
      RegSlot src = parse_assign_expr();
      emit_iABC(RegOp::SPREAD_OBJ, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(src.reg), 0);
      free_temp();
      goto obj_next;
    }

    // Property name: ident, string, number, or [
    if (lexer.token.type == TOK_IDENT || is_keyword(lexer.token.type)) {
      name = lexer.token.u.ident.atom;
      next_token();
      // Shorthand: { x } when no ':' follows
      if (lexer.token.type != ':' && lexer.token.type != '(') {
        // Look up x in scope and load it
        int val_reg = alloc_temp();
        // Try local vars first
        bool found = false;
        for (int i = 0; i < cur_func->var_count; i++) {
          if (cur_func->vars[static_cast<size_t>(i)].var_name == name && cur_func->vars[static_cast<size_t>(i)].scope_level == 0) {
            emit_iABC(RegOp::MOVE, static_cast<uint8_t>(val_reg), static_cast<uint8_t>(cur_func->vars[static_cast<size_t>(i)].reg_index), 0);
            found = true;
            break;
          }
        }
        if (!found) {
          int ci = cpool_add(Value::string(rt->atom_to_string(name)));
          emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(val_reg), static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
        }
        int ci = cpool_add(Value::string(rt->atom_to_string(name)));
        emit_iABC(RegOp::DEFINE_FIELD, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(val_reg));
        free_temp();
        free_temp();
        goto obj_next;
      }
      // Method shorthand: { foo() { } }
      if (lexer.token.type == '(') {
        FunctionDef *fd = parse_function_decl(kAtomNull, true, FunctionKind::normal);
        if (fd) {
          int r = alloc_temp();
          emit_iABx(RegOp::FCLOSURE, static_cast<uint8_t>(r), static_cast<uint16_t>(fd->parent_cpool_idx));
          int ci = cpool_add(Value::string(rt->atom_to_string(name)));
          emit_iABC(RegOp::DEFINE_FIELD, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(r));
          free_temp();
        }
        goto obj_next;
      }
    } else if (lexer.token.type == TOK_STRING) {
      auto sv = std::string_view{lexer.token.u.str.str, lexer.token.u.str.len};
      auto *s = String::create(sv);
      name    = rt->intern(s);
      next_token();
    } else if (lexer.token.type == TOK_NUMBER) {
      double d = lexer.token.u.num.val;
      char buf[32];
      snprintf(buf, sizeof(buf), "%.15g", d);
      auto *s = String::create(buf);
      name    = rt->intern(s);
      next_token();
    } else if (lexer.token.type == '[') {
      computed = true;
      next_token();
      RegSlot key = parse_assign_expr();
      expect(']');
      name = kAtomNull;
      if (!expect(':'))
        return {obj_reg};
      RegSlot val = parse_assign_expr();
      emit_iABC(RegOp::DEFINE_ELEM, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(key.reg), static_cast<uint8_t>(val.reg));
      free_temp(); // val
      free_temp(); // key
      goto obj_next;
    } else {
      goto obj_next;
    }

    if (!computed && name != kAtomNull) {
      if (!expect(':'))
        return {obj_reg};
      RegSlot val = parse_assign_expr();
      if (name == rt->intern(String::create("__proto__"))) {
        emit_iABC(RegOp::SETPROTO, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(val.reg), 0);
      } else {
        int ci = cpool_add(Value::string(rt->atom_to_string(name)));
        emit_iABC(RegOp::DEFINE_FIELD, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(val.reg));
      }
      free_temp();
    }

  obj_next:
    if (lexer.token.type != ',')
      break;
    next_token();
  }
  expect('}');
  return {obj_reg};
}

// ─── Array literal ──────────────────────────────────────────────────────

RegSlot RegParseState::parse_array_literal() {
  next_token(); // '['

  // Parse elements into a vector, collecting their register slots
  std::vector<int> elem_slots;
  while (lexer.token.type != ']') {
    if (lexer.token.type == ',') {
      next_token();
      continue;
    }
    if (lexer.token.type == TOK_ELLIPSIS)
      break; // spread not yet supported

    RegSlot elem = parse_assign_expr();
    int slot     = alloc_temp();
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(slot), static_cast<uint8_t>(elem.reg), 0);
    elem_slots.push_back(slot);
    // Don't free — keep slots contiguous. Element temps from
    // parse_assign_expr accumulate above our slots.

    if (lexer.token.type != ',')
      break;
    next_token();
  }
  expect(']');

  int count   = static_cast<int>(elem_slots.size());
  int arr_reg = alloc_temp();

  if (count == 0) {
    emit_iABC(RegOp::NEWARR, static_cast<uint8_t>(arr_reg), 0, 0);
  } else {
    // Move elements into contiguous block for NEWARR
    int base = alloc_temp();
    for (int i = 0; i < count; i++) {
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(base + i), static_cast<uint8_t>(elem_slots[static_cast<size_t>(i)]), 0);
    }
    cur_func->alloc.ensure_max(base + count);
    emit_iABC(RegOp::NEWARR, static_cast<uint8_t>(arr_reg), static_cast<uint8_t>(base), static_cast<uint8_t>(count));
    // Free the temporary contiguous block
    for (int i = 0; i < count; i++)
      free_temp();
  }

  // Free element slots
  for (int i = 0; i < count; i++)
    free_temp();

  return {arr_reg};
}

// ─── Postfix ────────────────────────────────────────────────────────────────

RegSlot RegParseState::parse_postfix_continue(RegSlot result, int flags) {
  for (;;) {
    int tok = lexer.token.type;

    if ((flags & PF_POSTFIX_CALL) && tok == '(') {
      next_token();
      int argc = 0;
      std::vector<int> arg_regs;
      if (lexer.token.type != ')') {
        for (;;) {
          RegSlot arg = parse_assign_expr();
          arg_regs.push_back(arg.reg);
          argc++;
          if (lexer.token.type == ')')
            break;
          if (lexer.token.type != ',') { /* error */
          }
          next_token();
        }
      }
      int func_reg = result.reg;
      for (int i = 0; i < argc; i++) {
        int tmp = alloc_temp();
        emit_iABC(RegOp::MOVE, static_cast<uint8_t>(tmp), static_cast<uint8_t>(arg_regs[static_cast<size_t>(i)]), 0);
        arg_regs[static_cast<size_t>(i)] = tmp;
      }
      int ret_reg = alloc_temp();
      emit_iABC(RegOp::CALL, static_cast<uint8_t>(ret_reg), static_cast<uint8_t>(func_reg), static_cast<uint8_t>(argc));
      for (int i = 0; i <= argc; i++)
        free_temp();
      result = {ret_reg};
      next_token();
      continue;
    }

    if (tok == '.') {
      next_token();
      if (lexer.token.type != TOK_IDENT)
        break;
      Atom prop = lexer.token.u.ident.atom;
      next_token();
      auto *s = rt->atom_to_string(prop);
      int ci  = cpool_add(Value::string(s));
      int dst = alloc_temp();
      emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst), static_cast<uint8_t>(result.reg), static_cast<uint8_t>(ci));
      free_temp();
      result = {dst};
      continue;
    }

    if (tok == '[') {
      next_token();
      RegSlot prop = parse_expr();
      expect(']');
      int dst = alloc_temp();
      emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(dst), static_cast<uint8_t>(result.reg), static_cast<uint8_t>(prop.reg));
      free_temp();
      free_temp();
      result = {dst};
      continue;
    }

    if (tok == TOK_INC) {
      next_token();
      int tmp = alloc_temp();
      emit_iABC(RegOp::INC, static_cast<uint8_t>(tmp), static_cast<uint8_t>(result.reg), 0);
      result = {tmp};
      continue;
    }
    if (tok == TOK_DEC) {
      next_token();
      int tmp = alloc_temp();
      emit_iABC(RegOp::DEC, static_cast<uint8_t>(tmp), static_cast<uint8_t>(result.reg), 0);
      result = {tmp};
      continue;
    }

    break;
  }
  return result;
}

RegSlot RegParseState::parse_postfix(int flags) { return parse_postfix_continue(parse_primary(), flags); }

// ─── Unary ──────────────────────────────────────────────────────────────────

RegSlot RegParseState::parse_unary(int flags) {
  int tok = lexer.token.type;

  switch (tok) {
  case '+':
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      // plus no-op, just return operand
      return opnd;
    }
  case '-':
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::NEG, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case '!':
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::LNOT, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case '~':
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::BNOT, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case TOK_TYPEOF:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::TYPEOF, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case TOK_VOID:
    next_token();
    {
      parse_unary(flags); // evaluate but discard
      int r = alloc_temp();
      emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(r), 0);
      return {r};
    }
  case TOK_DELETE:
    next_token();
    {
      parse_unary(flags);
      int r = alloc_temp();
      emit_iABx(RegOp::LOADTRUE, static_cast<uint8_t>(r), 0); // simplified
      return {r};
    }
  case TOK_INC:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::INC, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      // Store back (write to lvalue)
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(opnd.reg), static_cast<uint8_t>(r), 0);
      free_temp();
      return {r};
    }
  case TOK_DEC:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::DEC, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(opnd.reg), static_cast<uint8_t>(r), 0);
      free_temp();
      return {r};
    }
  default:
    return parse_postfix(flags);
  }
}

// ─── Binary (precedence climbing) ───────────────────────────────────────────

RegSlot RegParseState::parse_binary(int min_prec) {
  RegSlot left = parse_unary(PF_POSTFIX_CALL);

  for (;;) {
    int tok  = lexer.token.type;
    int prec = binary_precedence(tok);
    if (prec < min_prec)
      break;

    // ** is right-associative
    int next_min = (tok == TOK_POW) ? prec : prec + 1;

    // Short-circuit: &&, ||
    if (tok == TOK_LAND || tok == TOK_LOR) {
      next_token();
      int end_label = new_label();
      if (tok == TOK_LAND)
        emit_jump(RegOp::IS_FALSE, end_label, static_cast<uint8_t>(left.reg));
      else
        emit_jump(RegOp::IS_TRUE, end_label, static_cast<uint8_t>(left.reg));
      free_temp(); // free left (will be overwritten)

      RegSlot right = parse_binary(next_min);
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(left.reg), static_cast<uint8_t>(right.reg), 0);
      free_temp(); // free right
      emit_label(end_label);
      continue;
    }

    next_token();

    // Handle 'in' conditionally
    // (skip for now, just parse right side)

    RegSlot right = parse_binary(next_min);
    int r         = alloc_temp();
    RegOp op      = binop_to_reg(tok);
    if (op != RegOp::NOP) {
      emit_iABC(op, static_cast<uint8_t>(r), static_cast<uint8_t>(left.reg), static_cast<uint8_t>(right.reg));
    }
    free_temp(); // free right
    free_temp(); // free left
    left = {r};
  }
  return left;
}

// ─── Ternary ────────────────────────────────────────────────────────────────

RegSlot RegParseState::parse_cond_expr(int /*flags*/) {
  RegSlot cond = parse_binary(1); // min_prec=1 so non-operator tokens (prec=0) break

  if (lexer.token.type == '?') {
    next_token();
    int false_label = new_label();
    int end_label   = new_label();
    emit_jump(RegOp::IS_FALSE, false_label, static_cast<uint8_t>(cond.reg));
    free_temp();

    RegSlot then_val = parse_assign_expr();
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cond.reg), static_cast<uint8_t>(then_val.reg), 0);
    free_temp(); // free then_val
    emit_jump(RegOp::JMP, end_label, 0);

    emit_label(false_label);
    expect(':');
    RegSlot else_val = parse_assign_expr();
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cond.reg), static_cast<uint8_t>(else_val.reg), 0);
    free_temp(); // free else_val
    emit_label(end_label);
    return cond;
  }
  return cond;
}

// ─── Assignment ─────────────────────────────────────────────────────────────

LValue RegParseState::parse_lvalue() {
  LValue lv;
  int tok = lexer.token.type;

  if (tok == TOK_IDENT) {
    Atom atom = lexer.token.u.ident.atom;
    next_token();

    // Check if it's a local var
    for (int i = 0; i < cur_func->var_count; i++) {
      if (cur_func->vars[static_cast<size_t>(i)].var_name == atom && cur_func->vars[static_cast<size_t>(i)].scope_level == 0) {
        lv.kind    = LValue::LOCAL;
        lv.var_idx = i;
        lv.prop    = atom;
        return lv;
      }
    }
    // Check args
    for (int i = 0; i < cur_func->arg_count; i++) {
      if (cur_func->args[static_cast<size_t>(i)].var_name == atom) {
        lv.kind    = LValue::ARG;
        lv.var_idx = i;
        lv.prop    = atom;
        return lv;
      }
    }
    // Global fallback
    lv.kind = LValue::GLOBAL;
    lv.prop = atom;
    return lv;
  }

  // Member access: x.y
  if (tok == '.' && lexer.peek_token(false) == TOK_IDENT) {
    // This is called after parse_postfix for the LHS; the LHS is already parsed.
    // Actually, parse_lvalue is called BEFORE we know if it's an lvalue.
    // For now, simple case: ident only.
    // Complex lvalue support (x.y, x[y]) deferred.
  }

  // Default: treat as if it was parsed via primary + postfix
  // We assume the last emitted instruction(s) described the access.
  // For the initial implementation, support only simple identifier lvalues.
  return lv;
}

LValue RegParseState::parse_ident_lvalue() {
  LValue lv;
  Atom atom = lexer.token.u.ident.atom;
  next_token();

  for (int i = 0; i < cur_func->var_count; i++) {
    if (cur_func->vars[static_cast<size_t>(i)].var_name == atom) {
      lv.kind    = LValue::LOCAL;
      lv.var_idx = i;
      lv.prop    = atom;
      return lv;
    }
  }
  for (int i = 0; i < cur_func->arg_count; i++) {
    if (cur_func->args[static_cast<size_t>(i)].var_name == atom) {
      lv.kind    = LValue::ARG;
      lv.var_idx = i;
      lv.prop    = atom;
      return lv;
    }
  }
  // Check enclosing scopes (closure variable)
  int upval = cur_func->resolve_upval(atom);
  if (upval >= 0) {
    lv.kind       = LValue::UPVAL;
    lv.upval_idx  = upval;
    lv.prop       = atom;
    return lv;
  }
  lv.kind = LValue::GLOBAL;
  lv.prop = atom;
  return lv;
}

LValue RegParseState::parse_postfix_lvalue() {
  int tok = lexer.token.type;

  if (tok == TOK_IDENT) {
    LValue lv = parse_ident_lvalue();

    for (;;) {
      tok = lexer.token.type;
      if (tok == '.') {
        next_token();
        if (lexer.token.type != TOK_IDENT) { /*error*/
          break;
        }
        Atom prop = lexer.token.u.ident.atom;
        next_token();
        int obj_reg = alloc_temp();
        emit_lvalue_load(lv, {obj_reg});
        lv = {LValue::FIELD, obj_reg, prop};
        continue;
      }
      if (tok == '[') {
        next_token();
        RegSlot key = parse_expr();
        expect(']');
        int obj_reg = alloc_temp();
        emit_lvalue_load(lv, {obj_reg});
        lv = {LValue::ELEM, obj_reg, kAtomNull, key.reg};
        continue;
      }
      break;
    }
    return lv;
  }

  // Non-ident: parse primary then check for member access (for (expr).prop etc)
  RegSlot prim = parse_primary();
  LValue lv{LValue::FIELD, prim.reg, kAtomNull};

  for (;;) {
    tok = lexer.token.type;
    if (tok == '.') {
      next_token();
      if (lexer.token.type != TOK_IDENT) { /*error*/
        break;
      }
      Atom prop = lexer.token.u.ident.atom;
      next_token();
      lv.kind = LValue::FIELD;
      lv.prop = prop;
      continue;
    }
    if (tok == '[') {
      next_token();
      RegSlot key = parse_expr();
      expect(']');
      lv.kind    = LValue::ELEM;
      lv.key_reg = key.reg;
      lv.prop    = kAtomNull;
      continue;
    }
    break;
  }
  return lv;
}

RegSlot RegParseState::parse_binary_from(RegSlot left, int min_prec) {
  for (;;) {
    int tok  = lexer.token.type;
    int prec = binary_precedence(tok);
    if (prec < min_prec)
      break;

    int next_min = (tok == TOK_POW) ? prec : prec + 1;

    if (tok == TOK_LAND || tok == TOK_LOR) {
      next_token();
      int end_label = new_label();
      if (tok == TOK_LAND)
        emit_jump(RegOp::IS_FALSE, end_label, static_cast<uint8_t>(left.reg));
      else
        emit_jump(RegOp::IS_TRUE, end_label, static_cast<uint8_t>(left.reg));
      free_temp();

      RegSlot right = parse_binary(next_min);
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(left.reg), static_cast<uint8_t>(right.reg), 0);
      free_temp();
      emit_label(end_label);
      continue;
    }

    next_token();
    RegSlot right = parse_binary(next_min);
    int r         = alloc_temp();
    RegOp op      = binop_to_reg(tok);
    if (op != RegOp::NOP) {
      emit_iABC(op, static_cast<uint8_t>(r), static_cast<uint8_t>(left.reg), static_cast<uint8_t>(right.reg));
    }
    free_temp();
    free_temp();
    left = {r};
  }
  return left;
}

void RegParseState::emit_lvalue_load(LValue lv, RegSlot dst) {
  switch (lv.kind) {
  case LValue::LOCAL:
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(cur_func->vars[static_cast<size_t>(lv.var_idx)].reg_index), 0);
    break;
  case LValue::ARG:
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(cur_func->args[static_cast<size_t>(lv.var_idx)].reg_index), 0);
    break;
  case LValue::FIELD: {
    int ci = cpool_add(Value::string(rt->atom_to_string(lv.prop)));
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(ci));
    break;
  }
  case LValue::ELEM:
    emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(lv.key_reg));
    break;
  case LValue::GLOBAL: {
    int ci = cpool_add(Value::string(rt->atom_to_string(lv.prop)));
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst.reg),
              static_cast<uint8_t>(cur_func->alloc.this_reg()),
              static_cast<uint8_t>(ci));
    break;
  }
  case LValue::UPVAL:
    emit_iABC(RegOp::GETUPVAL, static_cast<uint8_t>(dst.reg),
              static_cast<uint8_t>(lv.upval_idx), 0);
    break;
  default:
    break;
  }
}

void RegParseState::emit_lvalue_store(LValue lv, RegSlot val) {
  switch (lv.kind) {
  case LValue::LOCAL:
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cur_func->vars[static_cast<size_t>(lv.var_idx)].reg_index), static_cast<uint8_t>(val.reg), 0);
    break;
  case LValue::ARG:
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cur_func->args[static_cast<size_t>(lv.var_idx)].reg_index), static_cast<uint8_t>(val.reg), 0);
    break;
  case LValue::FIELD: {
    int ci = cpool_add(Value::string(rt->atom_to_string(lv.prop)));
    emit_iABC(RegOp::SETFIELD, static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(val.reg));
    break;
  }
  case LValue::ELEM:
    emit_iABC(RegOp::SETELEM, static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(lv.key_reg), static_cast<uint8_t>(val.reg));
    break;
  case LValue::GLOBAL: {
    int ci = cpool_add(Value::string(rt->atom_to_string(lv.prop)));
    emit_iABC(RegOp::SETFIELD, static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci), static_cast<uint8_t>(val.reg));
    break;
  }
  case LValue::UPVAL:
    emit_iABC(RegOp::SETUPVAL, static_cast<uint8_t>(lv.upval_idx),
              static_cast<uint8_t>(val.reg), 0);
    break;
  default:
    break;
  }
}

RegSlot RegParseState::parse_assign_expr2(int flags) {
  int tok = lexer.token.type;

  // Try to parse an lvalue (identifier + optional member chains)
  LValue lv;
  bool is_lvalue = false;
  if (tok == TOK_IDENT || tok == '(') {
    lv        = parse_postfix_lvalue();
    is_lvalue = true;
  }

  if (is_lvalue) {
    int assign_tok = lexer.token.type;

    // Simple assignment: =
    if (assign_tok == '=') {
      next_token();
      RegSlot rhs = parse_assign_expr2(flags);
      emit_lvalue_store(lv, rhs);
      return rhs;
    }

    // Compound assignment: +=, -=, etc.
    if (assign_tok >= TOK_MUL_ASSIGN && assign_tok <= TOK_POW_ASSIGN) {
      next_token();
      RegSlot rhs = parse_assign_expr2(flags);
      int lhs_val = alloc_temp();
      emit_lvalue_load(lv, {lhs_val});
      int result  = alloc_temp();
      RegOp binop = compound_to_binop(assign_tok);
      if (binop != RegOp::NOP) {
        emit_iABC(binop, static_cast<uint8_t>(result), static_cast<uint8_t>(lhs_val), static_cast<uint8_t>(rhs.reg));
      }
      emit_lvalue_store(lv, {result});
      free_temp(); // rhs
      free_temp(); // result (after store)
      free_temp(); // lhs_val
      return {result};
    }

    // Logical assignment: &&=, ||=
    if (assign_tok >= TOK_LAND_ASSIGN && assign_tok <= TOK_DOUBLE_QUESTION_MARK_ASSIGN) {
      next_token();
      int lhs_val = alloc_temp();
      emit_lvalue_load(lv, {lhs_val});
      int end_label = new_label();
      if (assign_tok == TOK_LOR_ASSIGN)
        emit_jump(RegOp::IS_TRUE, end_label, static_cast<uint8_t>(lhs_val));
      else
        emit_jump(RegOp::IS_FALSE, end_label, static_cast<uint8_t>(lhs_val));
      free_temp();

      RegSlot rhs = parse_assign_expr2(flags);
      emit_lvalue_store(lv, rhs);
      free_temp();
      emit_label(end_label);
      int r = alloc_temp();
      emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(r), 0);
      return {r};
    }

    // Not an assignment — load from lvalue and continue
    RegSlot val = alloc_temp();
    emit_lvalue_load(lv, {val});
    val = parse_postfix_continue(val, flags | PF_POSTFIX_CALL);
    return parse_binary_from(val, 1);
  }

  return parse_cond_expr(flags);
}

RegSlot RegParseState::parse_assign_expr() { return parse_assign_expr2(PF_IN_ACCEPTED); }

RegSlot RegParseState::parse_expr() {
  bool comma = false;
  RegSlot result;
  for (;;) {
    result = parse_assign_expr2(PF_IN_ACCEPTED);
    if (comma) { /* drop intermediate results */
    }
    if (lexer.token.type != ',')
      break;
    comma = true;
    next_token();
  }
  return result;
}

// ─── Upvalue emission ─────────────────────────────────────────────────────

RegSlot RegParseState::emit_upval_read(int closure_idx) {
  int r = alloc_temp();
  emit_iABC(RegOp::GETUPVAL, static_cast<uint8_t>(r),
            static_cast<uint8_t>(closure_idx), 0);
  return {r};
}

void RegParseState::emit_upval_write(int closure_idx, RegSlot val) {
  emit_iABC(RegOp::SETUPVAL, static_cast<uint8_t>(closure_idx),
            static_cast<uint8_t>(val.reg), 0);
}

} // namespace qjsp
