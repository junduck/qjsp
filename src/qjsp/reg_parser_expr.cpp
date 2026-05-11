#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cstdio>

namespace qjsp {
// ═══════════════════════════════════════════════════════════════════════════
//  Expression parser
// ═══════════════════════════════════════════════════════════════════════════

RegSlot RegParseState::parse_primary() {
  TokenKind tok = lexer.token.kind;

  switch (tok) {
  case TokenKind::KwNull: {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADNULL, static_cast<uint8_t>(r), 0);
    next_token();
    return {r};
  }
  case TokenKind::KwFalse: {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADFALSE, static_cast<uint8_t>(r), 0);
    next_token();
    return {r};
  }
  case TokenKind::KwTrue: {
    int r = alloc_temp();
    emit_iABx(RegOp::LOADTRUE, static_cast<uint8_t>(r), 0);
    next_token();
    return {r};
  }
  case TokenKind::Number: {
    int r      = alloc_temp();
    double val = lexer.token.num_val;
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
  case TokenKind::StringLit: {
    int ci = cpool_add(String::create(std::string_view{lexer.token.str_val.c_str(), lexer.token.str_len}));
    int r  = alloc_temp();
    emit_iABx(RegOp::LOADK, static_cast<uint8_t>(r), static_cast<uint16_t>(ci));
    next_token();
    return {r};
  }
  case TokenKind::Identifier: {
    Atom atom = lexer.token.ident_atom;
    next_token();

    for (int i = 0; i < cur_func->var_count; i++) {
      if (cur_func->vars[static_cast<size_t>(i)].var_name == atom) {
        return {cur_func->vars[static_cast<size_t>(i)].reg_index};
      }
    }
    for (int i = 0; i < cur_func->arg_count; i++) {
      if (cur_func->args[static_cast<size_t>(i)].var_name == atom) {
        return {cur_func->args[static_cast<size_t>(i)].reg_index};
      }
    }
    int upval = cur_func->resolve_upval(atom);
    if (upval >= 0) {
      return emit_upval_read(upval);
    }
    int r  = alloc_temp();
    int ci = cpool_add(rt->atom_to_value(atom));
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(r), static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
    return {r};
  }
  case TokenKind::LParen: {
    next_token();
    RegSlot result = parse_expr();
    expect(TokenKind::RParen);
    return result;
  }
  case TokenKind::KwFunction: {
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
  case TokenKind::LBrace:
    return parse_object_literal();
  case TokenKind::LBracket:
    return parse_array_literal();
  default:
    return {alloc_temp()};
  }
}

// ─── Unified cover property parser ──────────────────────────────────────────

// Parses one { key: ... } or { key } entry. The caller decides how to
// handle shorthand vs key-value vs computed vs spread.
bool RegParseState::parse_cover_property(CoverProp &out) {
  TokenKind tok = lexer.token.kind;

  // Spread: ...expr
  if (tok == TokenKind::Ellipsis) {
    next_token();
    out.kind  = CoverProp::Spread;
    out.value = parse_assign_expr();
    return true;
  }

  // Identifier or keyword: {x}, {x: y}, or {x() {}}
  if (tok == TokenKind::Identifier || isKeyword(tok)) {
    out.key = lexer.token.ident_atom;
    next_token();

    // Shorthand: {x} — no colon, no paren
    if (lexer.token.kind != TokenKind::Colon && lexer.token.kind != TokenKind::LParen) {
      out.kind = CoverProp::Shorthand;
      return true;
    }

    // Method shorthand: {x() {}} — consume as KeyValue with FCLOSURE
    if (lexer.token.kind == TokenKind::LParen) {
      out.kind = CoverProp::KeyValue;
      FunctionDef *fd = parse_function_decl(kAtomNull, true, FunctionKind::normal);
      if (fd) {
        int r = alloc_temp();
        emit_iABx(RegOp::FCLOSURE, static_cast<uint8_t>(r), static_cast<uint16_t>(fd->parent_cpool_idx));
        out.value = {r};
      } else {
        out.value = {alloc_temp()};
        emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(out.value.reg), 0);
      }
      return true;
    }

    // Key-value: {x: expr}
    next_token(); // skip ':'
    out.kind  = CoverProp::KeyValue;
    out.value = parse_assign_expr();
    return true;
  }

  // String key: {"x": expr}
  if (tok == TokenKind::StringLit) {
    auto sv  = std::string_view{lexer.token.str_val.c_str(), lexer.token.str_len};
    out.key  = rt->intern(sv);
    next_token();
    if (lexer.token.kind != TokenKind::Colon) {
      out.kind = CoverProp::Shorthand; // { "x" } without colon — treat as shorthand
      return true;
    }
    next_token(); // skip ':'
    out.kind  = CoverProp::KeyValue;
    out.value = parse_assign_expr();
    return true;
  }

  // Number key: {42: expr}
  if (tok == TokenKind::Number) {
    double d = lexer.token.num_val;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.15g", d);
    out.key  = rt->intern(buf);
    next_token();
    if (!expect(TokenKind::Colon))
      return false;
    out.kind  = CoverProp::KeyValue;
    out.value = parse_assign_expr();
    return true;
  }

  // Computed key: {[expr]: val}
  if (tok == TokenKind::LBracket) {
    next_token();
    out.kind  = CoverProp::Computed;
    out.value = parse_assign_expr(); // the key expression
    expect(TokenKind::RBracket);
    return true;
  }

  return false;
}

// ─── Object literal ─────────────────────────────────────────────────────

RegSlot RegParseState::parse_object_literal() {
  next_token();

  int obj_reg = alloc_temp();
  emit_iABx(RegOp::NEWOBJ, static_cast<uint8_t>(obj_reg), 0);

  while (lexer.token.kind != TokenKind::RBrace) {
    CoverProp prop;
    if (!parse_cover_property(prop))
      break;

    switch (prop.kind) {
    case CoverProp::Shorthand: {
      // {x} — load variable x, define as property x
      int val_reg = alloc_temp();
      bool found  = false;
      for (int i = 0; i < cur_func->var_count; i++) {
        if (cur_func->vars[static_cast<size_t>(i)].var_name == prop.key && cur_func->vars[static_cast<size_t>(i)].scope_level == 0) {
          emit_iABC(RegOp::MOVE, static_cast<uint8_t>(val_reg), static_cast<uint8_t>(cur_func->vars[static_cast<size_t>(i)].reg_index), 0);
          found = true;
          break;
        }
      }
      if (!found) {
        int ci = cpool_add(rt->atom_to_value(prop.key));
        emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(val_reg), static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
      }
      int ci = cpool_add(rt->atom_to_value(prop.key));
      emit_iABC(RegOp::DEFINE_FIELD, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(val_reg));
      free_temp();
      free_temp();
      break;
    }
    case CoverProp::KeyValue: {
      if (prop.key == rt->intern("__proto__")) {
        emit_iABC(RegOp::SETPROTO, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(prop.value.reg), 0);
      } else {
        int ci = cpool_add(rt->atom_to_value(prop.key));
        emit_iABC(RegOp::DEFINE_FIELD, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(prop.value.reg));
      }
      free_temp(); // value
      break;
    }
    case CoverProp::Computed: {
      if (!expect(TokenKind::Colon))
        return {obj_reg};
      RegSlot val = parse_assign_expr();
      emit_iABC(RegOp::DEFINE_ELEM, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(prop.value.reg), static_cast<uint8_t>(val.reg));
      free_temp(); // val
      free_temp(); // key (prop.value)
      break;
    }
    case CoverProp::Spread:
      emit_iABC(RegOp::SPREAD_OBJ, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(prop.value.reg), 0);
      free_temp(); // value
      break;
    default:
      break;
    }

    if (lexer.token.kind != TokenKind::Comma)
      break;
    next_token();
  }
  expect(TokenKind::RBrace);
  return {obj_reg};
}

// ─── Array literal ──────────────────────────────────────────────────────

RegSlot RegParseState::parse_array_literal() {
  next_token();

  std::vector<int> elem_slots;
  while (lexer.token.kind != TokenKind::RBracket) {
    if (lexer.token.kind == TokenKind::Comma) {
      next_token();
      continue;
    }
    if (lexer.token.kind == TokenKind::Ellipsis)
      break;

    RegSlot elem = parse_assign_expr();
    int slot     = alloc_temp();
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(slot), static_cast<uint8_t>(elem.reg), 0);
    elem_slots.push_back(slot);

    if (lexer.token.kind != TokenKind::Comma)
      break;
    next_token();
  }
  expect(TokenKind::RBracket);

  int count   = static_cast<int>(elem_slots.size());
  int arr_reg = alloc_temp();

  if (count == 0) {
    emit_iABC(RegOp::NEWARR, static_cast<uint8_t>(arr_reg), 0, 0);
  } else {
    int base = alloc_temp();
    for (int i = 0; i < count; i++) {
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(base + i), static_cast<uint8_t>(elem_slots[static_cast<size_t>(i)]), 0);
    }
    cur_func->alloc.ensure_max(base + count);
    emit_iABC(RegOp::NEWARR, static_cast<uint8_t>(arr_reg), static_cast<uint8_t>(base), static_cast<uint8_t>(count));
    for (int i = 0; i < count; i++)
      free_temp();
  }

  for (int i = 0; i < count; i++)
    free_temp();

  return {arr_reg};
}

// ─── Postfix ────────────────────────────────────────────────────────────────

RegSlot RegParseState::parse_postfix_continue(RegSlot result, int flags) {
  for (;;) {
    TokenKind tok = lexer.token.kind;

    if ((flags & PF_POSTFIX_CALL) && tok == TokenKind::LParen) {
      next_token();
      int argc = 0;
      std::vector<int> arg_regs;
      if (lexer.token.kind != TokenKind::RParen) {
        for (;;) {
          RegSlot arg = parse_assign_expr();
          arg_regs.push_back(arg.reg);
          argc++;
          if (lexer.token.kind == TokenKind::RParen)
            break;
          if (lexer.token.kind != TokenKind::Comma) { /* error */
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

    if (tok == TokenKind::Dot) {
      next_token();
      if (lexer.token.kind != TokenKind::Identifier)
        break;
      Atom prop = lexer.token.ident_atom;
      next_token();
      int ci  = cpool_add(rt->atom_to_value(prop));
      int dst = alloc_temp();
      emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst), static_cast<uint8_t>(result.reg), static_cast<uint8_t>(ci));
      free_temp();
      result = {dst};
      continue;
    }

    if (tok == TokenKind::LBracket) {
      next_token();
      RegSlot prop = parse_expr();
      expect(TokenKind::RBracket);
      int dst = alloc_temp();
      emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(dst), static_cast<uint8_t>(result.reg), static_cast<uint8_t>(prop.reg));
      free_temp();
      free_temp();
      result = {dst};
      continue;
    }

    if (tok == TokenKind::Inc) {
      next_token();
      int tmp = alloc_temp();
      emit_iABC(RegOp::INC, static_cast<uint8_t>(tmp), static_cast<uint8_t>(result.reg), 0);
      result = {tmp};
      continue;
    }
    if (tok == TokenKind::Dec) {
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
  TokenKind tok = lexer.token.kind;

  switch (tok) {
  case TokenKind::Plus:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      return opnd;
    }
  case TokenKind::Minus:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::NEG, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case TokenKind::Bang:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::LNOT, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case TokenKind::Tilde:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::BNOT, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case TokenKind::KwTypeof:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::TYPEOF, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      free_temp();
      return {r};
    }
  case TokenKind::KwVoid:
    next_token();
    {
      parse_unary(flags);
      int r = alloc_temp();
      emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(r), 0);
      return {r};
    }
  case TokenKind::KwDelete:
    next_token();
    {
      parse_unary(flags);
      int r = alloc_temp();
      emit_iABx(RegOp::LOADTRUE, static_cast<uint8_t>(r), 0);
      return {r};
    }
  case TokenKind::Inc:
    next_token();
    {
      RegSlot opnd = parse_unary(flags);
      int r        = alloc_temp();
      emit_iABC(RegOp::INC, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
      emit_iABC(RegOp::MOVE, static_cast<uint8_t>(opnd.reg), static_cast<uint8_t>(r), 0);
      free_temp();
      return {r};
    }
  case TokenKind::Dec:
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
    TokenKind tok = lexer.token.kind;
    int prec      = binary_precedence(tok);
    if (prec < min_prec)
      break;

    int next_min = is_right_assoc(tok) ? prec : prec + 1;

    if (tok == TokenKind::Land || tok == TokenKind::Lor) {
      next_token();
      int end_label = new_label();
      if (tok == TokenKind::Land)
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

// ─── Ternary ────────────────────────────────────────────────────────────────

RegSlot RegParseState::parse_cond_expr(int /*flags*/) {
  RegSlot cond = parse_binary(1);

  if (lexer.token.kind == TokenKind::Question) {
    next_token();
    int false_label = new_label();
    int end_label   = new_label();
    emit_jump(RegOp::IS_FALSE, false_label, static_cast<uint8_t>(cond.reg));
    free_temp();

    RegSlot then_val = parse_assign_expr();
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cond.reg), static_cast<uint8_t>(then_val.reg), 0);
    free_temp();
    emit_jump(RegOp::JMP, end_label, 0);

    emit_label(false_label);
    expect(TokenKind::Colon);
    RegSlot else_val = parse_assign_expr();
    emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cond.reg), static_cast<uint8_t>(else_val.reg), 0);
    free_temp();
    emit_label(end_label);
    return cond;
  }
  return cond;
}

// ─── Assignment ─────────────────────────────────────────────────────────────

LValue RegParseState::parse_lvalue() {
  LValue lv;
  TokenKind tok = lexer.token.kind;

  if (tok == TokenKind::Identifier) {
    Atom atom = lexer.token.ident_atom;
    next_token();

    for (int i = 0; i < cur_func->var_count; i++) {
      if (cur_func->vars[static_cast<size_t>(i)].var_name == atom && cur_func->vars[static_cast<size_t>(i)].scope_level == 0) {
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
    lv.kind = LValue::GLOBAL;
    lv.prop = atom;
    return lv;
  }

  if (tok == TokenKind::Dot && lexer.peek_token(false) == TokenKind::Identifier) {
    // Complex lvalue support deferred.
  }

  return lv;
}

LValue RegParseState::parse_ident_lvalue() {
  LValue lv;
  Atom atom = lexer.token.ident_atom;
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
  int upval = cur_func->resolve_upval(atom);
  if (upval >= 0) {
    lv.kind      = LValue::UPVAL;
    lv.upval_idx = upval;
    lv.prop      = atom;
    return lv;
  }
  lv.kind = LValue::GLOBAL;
  lv.prop = atom;
  return lv;
}

LValue RegParseState::parse_postfix_lvalue() {
  TokenKind tok = lexer.token.kind;

  if (tok == TokenKind::Identifier) {
    LValue lv = parse_ident_lvalue();

    for (;;) {
      tok = lexer.token.kind;
      if (tok == TokenKind::Dot) {
        next_token();
        if (lexer.token.kind != TokenKind::Identifier) { /*error*/
          break;
        }
        Atom prop = lexer.token.ident_atom;
        next_token();
        int obj_reg = alloc_temp();
        emit_lvalue_load(lv, {obj_reg});
        lv = {LValue::FIELD, obj_reg, prop};
        continue;
      }
      if (tok == TokenKind::LBracket) {
        next_token();
        RegSlot key = parse_expr();
        expect(TokenKind::RBracket);
        int obj_reg = alloc_temp();
        emit_lvalue_load(lv, {obj_reg});
        lv = {LValue::ELEM, obj_reg, kAtomNull, key.reg};
        continue;
      }
      break;
    }
    return lv;
  }

  RegSlot prim = parse_primary();
  LValue lv{LValue::FIELD, prim.reg, kAtomNull};

  for (;;) {
    tok = lexer.token.kind;
    if (tok == TokenKind::Dot) {
      next_token();
      if (lexer.token.kind != TokenKind::Identifier) { /*error*/
        break;
      }
      Atom prop = lexer.token.ident_atom;
      next_token();
      lv.kind = LValue::FIELD;
      lv.prop = prop;
      continue;
    }
    if (tok == TokenKind::LBracket) {
      next_token();
      RegSlot key = parse_expr();
      expect(TokenKind::RBracket);
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
    TokenKind tok = lexer.token.kind;
    int prec      = binary_precedence(tok);
    if (prec < min_prec)
      break;

    int next_min = is_right_assoc(tok) ? prec : prec + 1;

    if (tok == TokenKind::Land || tok == TokenKind::Lor) {
      next_token();
      int end_label = new_label();
      if (tok == TokenKind::Land)
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
    int ci = cpool_add(rt->atom_to_value(lv.prop));
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(ci));
    break;
  }
  case LValue::ELEM:
    emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(lv.key_reg));
    break;
  case LValue::GLOBAL: {
    int ci = cpool_add(rt->atom_to_value(lv.prop));
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
    break;
  }
  case LValue::UPVAL:
    emit_iABC(RegOp::GETUPVAL, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(lv.upval_idx), 0);
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
    int ci = cpool_add(rt->atom_to_value(lv.prop));
    emit_iABC(RegOp::SETFIELD, static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(ci), static_cast<uint8_t>(val.reg));
    break;
  }
  case LValue::ELEM:
    emit_iABC(RegOp::SETELEM, static_cast<uint8_t>(lv.obj_reg), static_cast<uint8_t>(lv.key_reg), static_cast<uint8_t>(val.reg));
    break;
  case LValue::GLOBAL: {
    int ci = cpool_add(rt->atom_to_value(lv.prop));
    emit_iABC(RegOp::SETFIELD, static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci), static_cast<uint8_t>(val.reg));
    break;
  }
  case LValue::UPVAL:
    emit_iABC(RegOp::SETUPVAL, static_cast<uint8_t>(lv.upval_idx), static_cast<uint8_t>(val.reg), 0);
    break;
  default:
    break;
  }
}

namespace {
bool isArithAssign(TokenKind k) {
  return k == TokenKind::MulAssign || k == TokenKind::DivAssign || k == TokenKind::ModAssign || k == TokenKind::PlusAssign ||
         k == TokenKind::MinusAssign || k == TokenKind::ShlAssign || k == TokenKind::SarAssign || k == TokenKind::ShrAssign ||
         k == TokenKind::AndAssign || k == TokenKind::XorAssign || k == TokenKind::OrAssign || k == TokenKind::PowAssign;
}
bool isLogicAssign(TokenKind k) { return k == TokenKind::LandAssign || k == TokenKind::LorAssign || k == TokenKind::DoubleQuestionMarkAssign; }
} // namespace

RegSlot RegParseState::parse_assign_expr2(int flags) {
  TokenKind tok = lexer.token.kind;

  LValue lv;
  bool is_lvalue = false;
  if (tok == TokenKind::Identifier || tok == TokenKind::LParen) {
    lv        = parse_postfix_lvalue();
    is_lvalue = true;
  }

  if (is_lvalue) {
    TokenKind assign_tok = lexer.token.kind;

    if (assign_tok == TokenKind::EqAssign) {
      next_token();
      RegSlot rhs = parse_assign_expr2(flags);
      emit_lvalue_store(lv, rhs);
      return rhs;
    }

    if (isArithAssign(assign_tok)) {
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
      free_temp();
      free_temp();
      free_temp();
      return {result};
    }

    if (isLogicAssign(assign_tok)) {
      next_token();
      int lhs_val = alloc_temp();
      emit_lvalue_load(lv, {lhs_val});
      int end_label = new_label();
      if (assign_tok == TokenKind::LorAssign)
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
    if (lexer.token.kind != TokenKind::Comma)
      break;
    comma = true;
    next_token();
  }
  return result;
}

// ─── Upvalue emission ─────────────────────────────────────────────────────

RegSlot RegParseState::emit_upval_read(int closure_idx) {
  int r = alloc_temp();
  emit_iABC(RegOp::GETUPVAL, static_cast<uint8_t>(r), static_cast<uint8_t>(closure_idx), 0);
  return {r};
}

void RegParseState::emit_upval_write(int closure_idx, RegSlot val) {
  emit_iABC(RegOp::SETUPVAL, static_cast<uint8_t>(closure_idx), static_cast<uint8_t>(val.reg), 0);
}

} // namespace qjsp
