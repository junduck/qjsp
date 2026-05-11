#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cstdio>

namespace qjsp {
// ═══════════════════════════════════════════════════════════════════════════
//  Pratt expression parser
// ═══════════════════════════════════════════════════════════════════════════

// ─── Prefix handlers ────────────────────────────────────────────────────────

static RegSlot emit_literal_load(RegParseState *ps, int cpool_idx) {
  int r = ps->alloc_temp();
  ps->emit_iABx(RegOp::LOADK, static_cast<uint8_t>(r), static_cast<uint16_t>(cpool_idx));
  return {r};
}

static RegSlot parse_null_literal(RegParseState *ps) {
  int r = ps->alloc_temp();
  ps->emit_iABx(RegOp::LOADNULL, static_cast<uint8_t>(r), 0);
  ps->next_token();
  return {r};
}

static RegSlot parse_bool_literal(RegParseState *ps, bool val) {
  int r = ps->alloc_temp();
  ps->emit_iABx(val ? RegOp::LOADTRUE : RegOp::LOADFALSE, static_cast<uint8_t>(r), 0);
  ps->next_token();
  return {r};
}

static RegSlot parse_numeric(RegParseState *ps) {
  int r      = ps->alloc_temp();
  double val = ps->lexer.token.num_val;
  int32_t iv = static_cast<int32_t>(val);
  if (val == static_cast<double>(iv) && iv >= -32768 && iv <= 32767) {
    ps->emit_iAsBx(RegOp::LOADINT, static_cast<uint8_t>(r), static_cast<int16_t>(iv));
  } else {
    int ci = ps->cpool_add(Value::float64(val));
    ps->emit_iABx(RegOp::LOADK, static_cast<uint8_t>(r), static_cast<uint16_t>(ci));
  }
  ps->next_token();
  return {r};
}

static RegSlot parse_string_literal(RegParseState *ps) {
  int ci = ps->cpool_add(String::create(std::string_view{ps->lexer.token.str_val.c_str(), ps->lexer.token.str_len}));
  int r  = ps->alloc_temp();
  ps->emit_iABx(RegOp::LOADK, static_cast<uint8_t>(r), static_cast<uint16_t>(ci));
  ps->next_token();
  return {r};
}

static RegSlot parse_ident(RegParseState *ps) {
  Atom atom = ps->lexer.token.ident_atom;
  ps->next_token();

  // Check locals (backward to find innermost match first)
  for (int i = ps->cur_func->var_count; i-- > 0;) {
    if (ps->cur_func->vars[static_cast<size_t>(i)].var_name == atom) {
      ps->has_prefix_lvalue_ = true;
      ps->last_prefix_lvalue_ = {LValue::LOCAL, -1, atom, -1, i, -1};
      int r = ps->alloc_temp();
      ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(r), static_cast<uint8_t>(ps->cur_func->vars[static_cast<size_t>(i)].reg_index), 0);
      return {r};
    }
  }
  for (int i = ps->cur_func->arg_count; i-- > 0;) {
    if (ps->cur_func->args[static_cast<size_t>(i)].var_name == atom) {
      ps->has_prefix_lvalue_ = true;
      ps->last_prefix_lvalue_ = {LValue::ARG, -1, atom, -1, i, -1};
      int r = ps->alloc_temp();
      ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(r), static_cast<uint8_t>(ps->cur_func->args[static_cast<size_t>(i)].reg_index), 0);
      return {r};
    }
  }
  int upval = ps->cur_func->resolve_upval(atom);
  if (upval >= 0) {
    ps->has_prefix_lvalue_ = true;
    ps->last_prefix_lvalue_ = {LValue::UPVAL, -1, atom, -1, -1, upval};
    return ps->emit_upval_read(upval);
  }

  int r  = ps->alloc_temp();
  int ci = ps->cpool_add(ps->rt->atom_to_value(atom));
  ps->emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(r), static_cast<uint8_t>(ps->cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
  ps->has_prefix_lvalue_ = true;
  ps->last_prefix_lvalue_ = {LValue::GLOBAL, -1, atom};
  return {r};
}

static RegSlot parse_paren_group(RegParseState *ps) {
  ps->next_token(); // skip '('
  RegSlot result = ps->parse_expr();
  ps->expect(TokenKind::RParen);
  return result;
}

static RegSlot parse_function_expr(RegParseState *ps) {
  ps->next_token(); // skip 'function'
  auto *fd = ps->parse_function_decl(kAtomNull, true, FunctionKind::normal);
  if (!fd) {
    int r = ps->alloc_temp();
    ps->emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(r), 0);
    return {r};
  }
  int r = ps->alloc_temp();
  ps->emit_iABx(RegOp::FCLOSURE, static_cast<uint8_t>(r), static_cast<uint16_t>(fd->parent_cpool_idx));
  return {r};
}

static RegSlot parse_unary_prefix(RegParseState *ps, TokenKind op) {
  ps->next_token();
  switch (op) {
  case TokenKind::Plus:
    return ps->parse_expr(PREC_UNARY);
  case TokenKind::Minus: {
    RegSlot opnd = ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABC(RegOp::NEG, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
    ps->free_temp();
    return {r};
  }
  case TokenKind::Bang: {
    RegSlot opnd = ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABC(RegOp::LNOT, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
    ps->free_temp();
    return {r};
  }
  case TokenKind::Tilde: {
    RegSlot opnd = ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABC(RegOp::BNOT, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
    ps->free_temp();
    return {r};
  }
  case TokenKind::KwTypeof: {
    RegSlot opnd = ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABC(RegOp::TYPEOF, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
    ps->free_temp();
    return {r};
  }
  case TokenKind::KwVoid: {
    ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABx(RegOp::LOADUNDEF, static_cast<uint8_t>(r), 0);
    return {r};
  }
  case TokenKind::KwDelete: {
    ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABx(RegOp::LOADTRUE, static_cast<uint8_t>(r), 0);
    return {r};
  }
  case TokenKind::Inc: {
    RegSlot opnd = ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABC(RegOp::INC, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
    if (ps->has_prefix_lvalue_) {
      ps->has_prefix_lvalue_ = false;
      ps->emit_lvalue_store(ps->last_prefix_lvalue_, {r});
    } else {
      ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(opnd.reg), static_cast<uint8_t>(r), 0);
    }
    ps->free_temp();
    return {r};
  }
  case TokenKind::Dec: {
    RegSlot opnd = ps->parse_expr(PREC_UNARY);
    int r = ps->alloc_temp();
    ps->emit_iABC(RegOp::DEC, static_cast<uint8_t>(r), static_cast<uint8_t>(opnd.reg), 0);
    if (ps->has_prefix_lvalue_) {
      ps->has_prefix_lvalue_ = false;
      ps->emit_lvalue_store(ps->last_prefix_lvalue_, {r});
    } else {
      ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(opnd.reg), static_cast<uint8_t>(r), 0);
    }
    ps->free_temp();
    return {r};
  }
  default:
    return {ps->alloc_temp()};
  }
}

RegSlot RegParseState::parse_prefix() {
  TokenKind tok = lexer.token.kind;
  switch (tok) {
  case TokenKind::KwNull:      return parse_null_literal(this);
  case TokenKind::KwFalse:     return parse_bool_literal(this, false);
  case TokenKind::KwTrue:      return parse_bool_literal(this, true);
  case TokenKind::Number:      return parse_numeric(this);
  case TokenKind::StringLit:   return parse_string_literal(this);
  case TokenKind::Identifier:  return parse_ident(this);
  case TokenKind::LParen:      return parse_paren_group(this);
  case TokenKind::LBrace:      return parse_object_literal();
  case TokenKind::LBracket:    return parse_array_literal();
  case TokenKind::KwFunction:  return parse_function_expr(this);
  case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Bang:
  case TokenKind::Tilde: case TokenKind::KwTypeof: case TokenKind::KwVoid:
  case TokenKind::KwDelete: case TokenKind::Inc: case TokenKind::Dec:
    return parse_unary_prefix(this, tok);
  default:
    return {alloc_temp()};
  }
}

// ─── Infix handlers ─────────────────────────────────────────────────────────

static RegSlot parse_call_infix(RegParseState *ps, RegSlot func) {
  ps->next_token(); // skip '('
  int argc = 0;
  std::vector<int> arg_regs;
  if (ps->lexer.token.kind != TokenKind::RParen) {
    for (;;) {
      RegSlot arg = ps->parse_assign_expr();
      arg_regs.push_back(arg.reg);
      argc++;
      if (ps->lexer.token.kind == TokenKind::RParen)
        break;
      if (ps->lexer.token.kind != TokenKind::Comma) { /* error */ }
      ps->next_token();
    }
  }
  int func_reg = func.reg;
  for (int i = 0; i < argc; i++) {
    int tmp = ps->alloc_temp();
    ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(tmp), static_cast<uint8_t>(arg_regs[static_cast<size_t>(i)]), 0);
    arg_regs[static_cast<size_t>(i)] = tmp;
  }
  int ret_reg = ps->alloc_temp();
  ps->emit_iABC(RegOp::CALL, static_cast<uint8_t>(ret_reg), static_cast<uint8_t>(func_reg), static_cast<uint8_t>(argc));
  for (int i = 0; i <= argc; i++)
    ps->free_temp();
  ps->next_token();
  return RegSlot{ret_reg};
}

static RegSlot parse_dot_infix(RegParseState *ps, RegSlot obj) {
  ps->next_token(); // skip '.'
  if (ps->lexer.token.kind != TokenKind::Identifier)
    return obj;
  Atom prop = ps->lexer.token.ident_atom;
  ps->next_token();
  int ci  = ps->cpool_add(ps->rt->atom_to_value(prop));
  int dst = ps->alloc_temp();
  ps->emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst), static_cast<uint8_t>(obj.reg), static_cast<uint8_t>(ci));
  ps->free_temp();
  return RegSlot{dst};
}

static RegSlot parse_bracket_infix(RegParseState *ps, RegSlot obj) {
  ps->next_token(); // skip '['
  RegSlot prop = ps->parse_expr();
  ps->expect(TokenKind::RBracket);
  int dst = ps->alloc_temp();
  ps->emit_iABC(RegOp::GETELEM, static_cast<uint8_t>(dst), static_cast<uint8_t>(obj.reg), static_cast<uint8_t>(prop.reg));
  ps->free_temp();
  ps->free_temp();
  return RegSlot{dst};
}

static RegSlot parse_postfix_incdec(RegParseState *ps, RegSlot base, TokenKind tok) {
  ps->next_token();

  RegOp op = (tok == TokenKind::Inc) ? RegOp::INC : RegOp::DEC;

  if (ps->has_prefix_lvalue_) {
    ps->has_prefix_lvalue_ = false;
    LValue lv = ps->last_prefix_lvalue_;

    // base is a temp holding the current value (loaded by parse_ident)
    int old_r = base.reg;

    int new_r = ps->alloc_temp();
    ps->emit_iABC(op, static_cast<uint8_t>(new_r), static_cast<uint8_t>(old_r), 0);
    // Store new value back to the actual variable
    ps->emit_lvalue_store(lv, {new_r});
    ps->free_temp(); // new_r
    return {old_r};   // postfix returns old value
  }

  // Fallback for non-identifier operands (e.g. 3++ which is invalid JS,
  // or cases where the lvalue wasn't captured)
  int tmp = ps->alloc_temp();
  ps->emit_iABC(op, static_cast<uint8_t>(tmp), static_cast<uint8_t>(base.reg), 0);
  return RegSlot{tmp};
}

static RegSlot parse_binary_infix(RegParseState *ps, RegSlot left, TokenKind tok) {
  int min_prec = static_cast<int>(kInfixTable[static_cast<uint16_t>(tok)].prec);
  int next_min = is_right_assoc(tok) ? min_prec : min_prec + 1;

  // Short-circuit: &&, ||
  if (tok == TokenKind::Land || tok == TokenKind::Lor) {
    ps->next_token();
    int end_label = ps->new_label();
    if (tok == TokenKind::Land)
      ps->emit_jump(RegOp::IS_FALSE, end_label, static_cast<uint8_t>(left.reg));
    else
      ps->emit_jump(RegOp::IS_TRUE, end_label, static_cast<uint8_t>(left.reg));
    ps->free_temp();

    RegSlot right = ps->parse_expr(next_min);
    ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(left.reg), static_cast<uint8_t>(right.reg), 0);
    ps->free_temp();
    ps->emit_label(end_label);
    return left;
  }

  ps->next_token();
  RegSlot right = ps->parse_expr(next_min);
  int r = ps->alloc_temp();
  RegOp op = binop_to_reg(tok);
  if (op != RegOp::NOP)
    ps->emit_iABC(op, static_cast<uint8_t>(r), static_cast<uint8_t>(left.reg), static_cast<uint8_t>(right.reg));
  ps->free_temp(); // right
  ps->free_temp(); // left
  return RegSlot{r};
}

static RegSlot parse_ternary_infix(RegParseState *ps, RegSlot cond) {
  ps->next_token(); // skip '?'
  int false_label = ps->new_label();
  int end_label   = ps->new_label();
  ps->emit_jump(RegOp::IS_FALSE, false_label, static_cast<uint8_t>(cond.reg));
  ps->free_temp();

  RegSlot then_val = ps->parse_assign_expr();
  ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cond.reg), static_cast<uint8_t>(then_val.reg), 0);
  ps->free_temp();
  ps->emit_jump(RegOp::JMP, end_label, 0);

  ps->emit_label(false_label);
  ps->expect(TokenKind::Colon);
  RegSlot else_val = ps->parse_assign_expr();
  ps->emit_iABC(RegOp::MOVE, static_cast<uint8_t>(cond.reg), static_cast<uint8_t>(else_val.reg), 0);
  ps->free_temp();
  ps->emit_label(end_label);
  return cond;
}

static bool is_logic_assign_op(TokenKind tok) {
  return tok == TokenKind::LandAssign || tok == TokenKind::LorAssign || tok == TokenKind::DoubleQuestionMarkAssign;
}

RegSlot RegParseState::parse_infix(RegSlot left, TokenKind tok) {
  if (tok == TokenKind::Inc || tok == TokenKind::Dec) return parse_postfix_incdec(this, left, tok);

  // Any infix operator other than INC/DEC invalidates the lvalue tracking
  has_prefix_lvalue_ = false;

  if (tok == TokenKind::LParen) return parse_call_infix(this, left);
  if (tok == TokenKind::Dot)    return parse_dot_infix(this, left);
  if (tok == TokenKind::LBracket) return parse_bracket_infix(this, left);
  if (tok == TokenKind::Question) return parse_ternary_infix(this, left);

  // Binary / logical operators
  auto prec = kInfixTable[static_cast<uint16_t>(tok)].prec;
  if ((prec >= PREC_BIT_OR && prec <= PREC_POW) || prec == PREC_LOGICAL_AND || prec == PREC_LOGICAL_OR)
    return parse_binary_infix(this, left, tok);

  // Assignment ops: not handled here — parse_assign_expr handles them via lvalue dispatch.
  // We must not fall through to the main loop without consuming the token.
  return left;
}

// ─── Pratt loop + top-level entry ───────────────────────────────────────────

// Continue parsing infix operators from an existing left operand
static RegSlot parse_expr_from(RegParseState *ps, RegSlot left, int min_prec) {
  for (;;) {
    TokenKind tok = ps->lexer.token.kind;
    auto &entry = kInfixTable[static_cast<uint16_t>(tok)];
    if (entry.prec < min_prec) break;
    left = ps->parse_infix(left, tok);
  }
  return left;
}

// Top-level expression entry — handles both assignments and pure expressions
RegSlot RegParseState::parse_expr(int min_prec) {
  if (min_prec <= 1)
    return parse_assign_expr();

  RegSlot left = parse_prefix();
  return parse_expr_from(this, left, min_prec);
}

// ─── Assignment (lvalue-based, delegates to Pratt for RHS) ──────────────────

static bool is_arith_assign(TokenKind tok) {
  return tok >= TokenKind::MulAssign && tok <= TokenKind::PowAssign;
}

RegSlot RegParseState::parse_assign_expr() {
  TokenKind tok = lexer.token.kind;

  if (tok == TokenKind::Identifier || tok == TokenKind::LParen) {
    LValue lv   = parse_postfix_lvalue();
    has_prefix_lvalue_ = true;
    last_prefix_lvalue_ = lv;
    TokenKind assign_tok = lexer.token.kind;

    if (assign_tok == TokenKind::EqAssign) {
      has_prefix_lvalue_ = false;
      next_token();
      RegSlot rhs = parse_expr(PREC_ASSIGN);
      emit_lvalue_store(lv, rhs);
      return rhs;
    }

    if (is_arith_assign(assign_tok)) {
      has_prefix_lvalue_ = false;
      next_token();
      RegSlot rhs   = parse_expr(PREC_ASSIGN);
      int lhs_val   = alloc_temp();
      emit_lvalue_load(lv, {lhs_val});
      int result    = alloc_temp();
      RegOp binop   = compound_to_binop(assign_tok);
      if (binop != RegOp::NOP)
        emit_iABC(binop, static_cast<uint8_t>(result), static_cast<uint8_t>(lhs_val), static_cast<uint8_t>(rhs.reg));
      emit_lvalue_store(lv, {result});
      free_temp(); // rhs
      free_temp(); // result
      free_temp(); // lhs_val
      return {result};
    }

    if (is_logic_assign_op(assign_tok)) {
      has_prefix_lvalue_ = false;
      next_token();
      int lhs_val = alloc_temp();
      emit_lvalue_load(lv, {lhs_val});
      int end_label = new_label();
      if (assign_tok == TokenKind::LorAssign)
        emit_jump(RegOp::IS_TRUE, end_label, static_cast<uint8_t>(lhs_val));
      else
        emit_jump(RegOp::IS_FALSE, end_label, static_cast<uint8_t>(lhs_val));
      free_temp();
      RegSlot rhs = parse_expr(PREC_ASSIGN);
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
    // Use the Pratt parser for postfix/binary continuation (no assignment)
    return parse_expr_from(this, val, PREC_ASSIGN + 1);  // stop before any assignment
  }

  // Not an lvalue — use pure Pratt parser
  RegSlot left = parse_prefix();
  return parse_expr_from(this, left, 1);
}

// ─── Object literal ─────────────────────────────────────────────────────

RegSlot RegParseState::parse_object_literal() {
  next_token(); // skip '{'

  int obj_reg = alloc_temp();
  emit_iABx(RegOp::NEWOBJ, static_cast<uint8_t>(obj_reg), 0);

  while (lexer.token.kind != TokenKind::RBrace) {
    CoverProp prop;
    if (!parse_cover_property(prop))
      break;

    switch (prop.kind) {
    case CoverProp::Shorthand: {
      int val_reg = alloc_temp();
      bool found  = false;
      for (int i = cur_func->var_count; i-- > 0;) {
        if (cur_func->vars[static_cast<size_t>(i)].var_name == prop.key) {
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
      free_temp();
      break;
    }
    case CoverProp::Computed: {
      if (!expect(TokenKind::Colon))
        return {obj_reg};
      RegSlot val = parse_assign_expr();
      emit_iABC(RegOp::DEFINE_ELEM, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(prop.value.reg), static_cast<uint8_t>(val.reg));
      free_temp();
      free_temp();
      break;
    }
    case CoverProp::Spread:
      emit_iABC(RegOp::SPREAD_OBJ, static_cast<uint8_t>(obj_reg), static_cast<uint8_t>(prop.value.reg), 0);
      free_temp();
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

// ─── Unified cover property parser ──────────────────────────────────────────

bool RegParseState::parse_cover_property(CoverProp &out) {
  TokenKind tok = lexer.token.kind;

  if (tok == TokenKind::Ellipsis) {
    next_token();
    out.kind  = CoverProp::Spread;
    out.value = parse_assign_expr();
    return true;
  }

  if (tok == TokenKind::Identifier || isKeyword(tok)) {
    out.key = lexer.token.ident_atom;
    next_token();

    if (lexer.token.kind != TokenKind::Colon && lexer.token.kind != TokenKind::LParen) {
      out.kind = CoverProp::Shorthand;
      return true;
    }

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

    next_token();
    out.kind  = CoverProp::KeyValue;
    out.value = parse_assign_expr();
    return true;
  }

  if (tok == TokenKind::StringLit) {
    auto sv = std::string_view{lexer.token.str_val.c_str(), lexer.token.str_len};
    out.key = rt->intern(sv);
    next_token();
    if (lexer.token.kind != TokenKind::Colon) {
      out.kind = CoverProp::Shorthand;
      return true;
    }
    next_token();
    out.kind  = CoverProp::KeyValue;
    out.value = parse_assign_expr();
    return true;
  }

  if (tok == TokenKind::Number) {
    double d = lexer.token.num_val;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.15g", d);
    out.key = rt->intern(buf);
    next_token();
    if (!expect(TokenKind::Colon))
      return false;
    out.kind  = CoverProp::KeyValue;
    out.value = parse_assign_expr();
    return true;
  }

  if (tok == TokenKind::LBracket) {
    next_token();
    out.kind  = CoverProp::Computed;
    out.value = parse_assign_expr();
    expect(TokenKind::RBracket);
    return true;
  }

  return false;
}

// ─── Array literal ──────────────────────────────────────────────────────

RegSlot RegParseState::parse_array_literal() {
  next_token(); // skip '['

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

// ─── LValue (for LHS of assignments) ────────────────────────────────────

LValue RegParseState::parse_lvalue() {
  LValue lv;
  TokenKind tok = lexer.token.kind;

  if (tok == TokenKind::Identifier) {
    Atom atom = lexer.token.ident_atom;
    next_token();

    for (int i = cur_func->var_count; i-- > 0;) {
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
    lv.kind = LValue::GLOBAL;
    lv.prop = atom;
    return lv;
  }

  if (tok == TokenKind::Dot && peek_token(false) == TokenKind::Identifier) {
    // Complex lvalue support deferred.
  }

  return lv;
}

LValue RegParseState::parse_ident_lvalue() {
  LValue lv;
  Atom atom = lexer.token.ident_atom;
  next_token();

  for (int i = cur_func->var_count; i-- > 0;) {
    if (cur_func->vars[static_cast<size_t>(i)].var_name == atom) {
      lv.kind    = LValue::LOCAL;
      lv.var_idx = i;
      lv.prop    = atom;
      return lv;
    }
  }
  for (int i = cur_func->arg_count; i-- > 0;) {
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
        if (lexer.token.kind != TokenKind::Identifier) break;
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

  RegSlot prim = parse_prefix();
  LValue lv{LValue::FIELD, prim.reg, kAtomNull};

  for (;;) {
    tok = lexer.token.kind;
    if (tok == TokenKind::Dot) {
      next_token();
      if (lexer.token.kind != TokenKind::Identifier) break;
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
