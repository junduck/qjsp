#include "qjsp/reg_parser.hpp"
#include "qjsp/context.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace qjsp {

// ─── Helpers ────────────────────────────────────────────────────────────────

int binary_precedence(int tok) {
  switch (tok) {
  case '*':
  case '/':
  case '%':
    return PREC_MULT;
  case '+':
  case '-':
    return PREC_ADD;
  case TOK_SHL:
  case TOK_SAR:
  case TOK_SHR:
    return PREC_SHIFT;
  case '<':
  case '>':
  case TOK_LTE:
  case TOK_GTE:
    return PREC_COMPARE;
  case TOK_EQ:
  case TOK_NEQ:
  case TOK_STRICT_EQ:
  case TOK_STRICT_NEQ:
    return PREC_EQ;
  case '&':
    return PREC_BIT_AND;
  case '^':
    return PREC_BIT_XOR;
  case '|':
    return PREC_BIT_OR;
  case TOK_LAND:
    return PREC_LOGICAL_AND;
  case TOK_LOR:
    return PREC_LOGICAL_OR;
  case TOK_POW:
    return PREC_POW;
  case TOK_DOUBLE_QUESTION_MARK:
    return PREC_LOGICAL_OR;
  default:
    return 0;
  }
}

RegOp binop_to_reg(int tok) {
  switch (tok) {
  case '*':
    return RegOp::MUL;
  case '/':
    return RegOp::DIV;
  case '%':
    return RegOp::MOD;
  case '+':
    return RegOp::ADD;
  case '-':
    return RegOp::SUB;
  case TOK_SHL:
    return RegOp::SHL;
  case TOK_SAR:
    return RegOp::SAR;
  case TOK_SHR:
    return RegOp::SHR;
  case '<':
    return RegOp::LT;
  case '>':
    return RegOp::GT;
  case TOK_LTE:
    return RegOp::LTE;
  case TOK_GTE:
    return RegOp::GTE;
  case TOK_EQ:
    return RegOp::EQ;
  case TOK_NEQ:
    return RegOp::NEQ;
  case TOK_STRICT_EQ:
    return RegOp::SEQ;
  case TOK_STRICT_NEQ:
    return RegOp::SNEQ;
  case '&':
    return RegOp::AND;
  case '^':
    return RegOp::XOR;
  case '|':
    return RegOp::OR;
  case TOK_POW:
    return RegOp::POW;
  default:
    return RegOp::NOP;
  }
}

static RegOp compound_to_binop(int tok) {
  switch (tok) {
  case TOK_MUL_ASSIGN:
    return RegOp::MUL;
  case TOK_DIV_ASSIGN:
    return RegOp::DIV;
  case TOK_MOD_ASSIGN:
    return RegOp::MOD;
  case TOK_PLUS_ASSIGN:
    return RegOp::ADD;
  case TOK_MINUS_ASSIGN:
    return RegOp::SUB;
  case TOK_SHL_ASSIGN:
    return RegOp::SHL;
  case TOK_SAR_ASSIGN:
    return RegOp::SAR;
  case TOK_SHR_ASSIGN:
    return RegOp::SHR;
  case TOK_AND_ASSIGN:
    return RegOp::AND;
  case TOK_OR_ASSIGN:
    return RegOp::OR;
  case TOK_XOR_ASSIGN:
    return RegOp::XOR;
  case TOK_POW_ASSIGN:
    return RegOp::POW;
  default:
    return RegOp::NOP;
  }
}

// ─── Truthiness ─────────────────────────────────────────────────────────────

static bool is_truthy(Value v) {
  if (v.is_null() || v.is_undefined())
    return false;
  if (v.is_bool())
    return v.as_bool();
  if (v.is_int32())
    return v.as_int32() != 0;
  if (v.is_double())
    return v.as_double() != 0.0;
  return true;
}

// ─── FunctionDef emitter ────────────────────────────────────────────────────

void FunctionDef::emit_iABC(RegOp op, uint8_t a, uint8_t b, uint8_t c) {
  instructions.push_back(Instruction::iABC(static_cast<uint8_t>(op), a, b, c).raw);
  last_instr_index = static_cast<int>(instructions.size()) - 1;
}

void FunctionDef::emit_iABx(RegOp op, uint8_t a, uint16_t bx) {
  instructions.push_back(Instruction::iABx(static_cast<uint8_t>(op), a, bx).raw);
  last_instr_index = static_cast<int>(instructions.size()) - 1;
}

void FunctionDef::emit_iAsBx(RegOp op, uint8_t a, int16_t sbx) {
  instructions.push_back(Instruction::iAsBx(static_cast<uint8_t>(op), a, sbx).raw);
  last_instr_index = static_cast<int>(instructions.size()) - 1;
}

int FunctionDef::new_label() {
  int id = static_cast<int>(label_slots.size());
  label_slots.emplace_back();
  return id;
}

int FunctionDef::emit_label(int id) {
  label_slots[static_cast<size_t>(id)].pos = static_cast<int>(instructions.size());
  return label_slots[static_cast<size_t>(id)].pos;
}

// Emit a conditional or unconditional jump. Returns the instruction index.
int FunctionDef::emit_jump(RegOp op, int label, uint8_t reg_a) {
  if (label < 0)
    return -1;
  label_slots[static_cast<size_t>(label)].ref_count++;
  emit_iAsBx(op, reg_a, 0); // placeholder offset
  patches.push_back({static_cast<int>(instructions.size()) - 1, label});
  return static_cast<int>(instructions.size()) - 1;
}

int FunctionDef::cpool_add(Value val) {
  cpool.push_back(val);
  return static_cast<int>(cpool.size()) - 1;
}

// ─── FunctionDef scope / vars ───────────────────────────────────────────────

int FunctionDef::find_arg(Atom name) {
  for (int i = arg_count; i-- > 0;)
    if (args[static_cast<size_t>(i)].var_name == name)
      return i;
  return -1;
}

int FunctionDef::find_var(Atom name) {
  for (int i = var_count; i-- > 0;)
    if (vars[static_cast<size_t>(i)].var_name == name && vars[static_cast<size_t>(i)].scope_level == 0)
      return i;
  return find_arg(name);
}

VarDef *FunctionDef::find_scope_var(Atom name, int scope) {
  int idx = scopes[static_cast<size_t>(scope)].first;
  while (idx >= 0) {
    VarDef &vd = vars[static_cast<size_t>(idx)];
    if (vd.scope_level != scope)
      break;
    if (vd.var_name == name)
      return &vd;
    idx = vd.scope_next;
  }
  return nullptr;
}

int FunctionDef::add_var(Atom name) {
  int idx = var_count++;
  vars.emplace_back();
  vars[static_cast<size_t>(idx)].var_name  = name;
  vars[static_cast<size_t>(idx)].reg_index = 1 + arg_count + idx;
  // Update alloc so temps don't collide with var slots
  alloc.var_count_ = var_count;
  int min_temp     = 1 + alloc.arg_count_ + alloc.var_count_;
  if (alloc.next_temp_ < min_temp)
    alloc.next_temp_ = min_temp;
  if (alloc.max_temp_ < min_temp)
    alloc.max_temp_ = min_temp;
  return idx;
}

int FunctionDef::add_arg(Atom name) {
  int idx = arg_count++;
  args.emplace_back();
  args[static_cast<size_t>(idx)].var_name  = name;
  args[static_cast<size_t>(idx)].reg_index = 1 + idx;
  alloc.arg_count_                         = arg_count;
  int min_temp                             = 1 + alloc.arg_count_ + alloc.var_count_;
  if (alloc.next_temp_ < min_temp)
    alloc.next_temp_ = min_temp;
  if (alloc.max_temp_ < min_temp)
    alloc.max_temp_ = min_temp;
  return idx;
}

static int get_first_lexical_var(FunctionDef *fd, int scope) {
  while (scope >= 0) {
    int idx = fd->scopes[static_cast<size_t>(scope)].first;
    if (idx >= 0)
      return idx;
    scope = fd->scopes[static_cast<size_t>(scope)].parent;
  }
  return -1;
}

int FunctionDef::push_scope() {
  int scope = static_cast<int>(scopes.size());
  scopes.emplace_back();
  scopes[static_cast<size_t>(scope)].parent = scope_level;
  scopes[static_cast<size_t>(scope)].first  = scope_first;
  emit_iABx(RegOp::NOP, 0, 0); // scope marker (placeholder)
  scope_level = scope;
  return scope;
}

void FunctionDef::pop_scope() {
  int scope   = scope_level;
  scope_level = scopes[static_cast<size_t>(scope)].parent;
  scope_first = get_first_lexical_var(this, scope_level);
  emit_iABx(RegOp::NOP, 0, 0); // scope exit marker (placeholder)
}

void FunctionDef::close_scopes(int scope, int scope_stop) {
  while (scope > scope_stop) {
    scope = scopes[static_cast<size_t>(scope)].parent;
    emit_iABx(RegOp::NOP, 0, 0);
  }
}

void FunctionDef::push_break(BlockEnv *be, Atom label, int lbreak, int lcont) {
  be->prev        = top_break;
  top_break       = be;
  be->label_name  = label;
  be->label_break = lbreak;
  be->label_cont  = lcont;
  be->scope_level = scope_level;
}

void FunctionDef::pop_break() { top_break = top_break->prev; }

// ─── RegParseState ──────────────────────────────────────────────────────────

void RegParseState::init(const char *source, const char *filename) {
  lexer.init(rt, filename, reinterpret_cast<const uint8_t *>(source), std::strlen(source));
}

bool RegParseState::expect(int tok) {
  if (lexer.token.type != tok)
    return false;
  return next_token();
}

bool RegParseState::js_define_var(Atom name, int tok) {
  FunctionDef *fd = cur_func;
  switch (tok) {
  case TOK_LET:
    if (fd->find_scope_var(name, fd->scope_level))
      return false;
    {
      int idx = fd->add_var(name);
      if (idx < 0)
        return false;
      VarDef &vd                                             = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind                                            = static_cast<uint8_t>(VarDefKind::let);
      vd.scope_level                                         = fd->scope_level;
      vd.scope_next                                          = fd->scope_first;
      vd.is_lexical                                          = true;
      fd->scopes[static_cast<size_t>(fd->scope_level)].first = idx;
      fd->scope_first                                        = idx;
    }
    return true;
  case TOK_CONST:
    if (fd->find_scope_var(name, fd->scope_level))
      return false;
    {
      int idx = fd->add_var(name);
      if (idx < 0)
        return false;
      VarDef &vd                                             = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind                                            = static_cast<uint8_t>(VarDefKind::const_);
      vd.scope_level                                         = fd->scope_level;
      vd.scope_next                                          = fd->scope_first;
      vd.is_lexical                                          = true;
      vd.is_const                                            = true;
      fd->scopes[static_cast<size_t>(fd->scope_level)].first = idx;
      fd->scope_first                                        = idx;
    }
    return true;
  case TOK_VAR:
    if (fd->find_var(name) < 0) {
      int idx = fd->add_var(name);
      if (idx < 0)
        return false;
      VarDef &vd     = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind    = static_cast<uint8_t>(VarDefKind::var_);
      vd.scope_level = 0;
      vd.scope_next  = -1;
    }
    return true;
  default:
    return false;
  }
}

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
    double val = lexer.token.u.num.val;
    int r      = alloc_temp();
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

    // Check function-scope vars
    for (int i = 0; i < cur_func->var_count; i++) {
      if (cur_func->vars[static_cast<size_t>(i)].var_name == atom && cur_func->vars[static_cast<size_t>(i)].scope_level == 0) {
        return {cur_func->vars[static_cast<size_t>(i)].reg_index};
      }
    }
    // Check args
    for (int i = 0; i < cur_func->arg_count; i++) {
      if (cur_func->args[static_cast<size_t>(i)].var_name == atom) {
        return {cur_func->args[static_cast<size_t>(i)].reg_index};
      }
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

RegSlot RegParseState::parse_cond_expr(int flags) {
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
    emit_iABC(RegOp::GETFIELD, static_cast<uint8_t>(dst.reg), static_cast<uint8_t>(cur_func->alloc.this_reg()), static_cast<uint8_t>(ci));
    break;
  }
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

// ═══════════════════════════════════════════════════════════════════════════
//  Statement parser
// ═══════════════════════════════════════════════════════════════════════════

void RegParseState::parse_statement() { parse_statement_or_decl(0); }

void RegParseState::parse_statement_or_decl(int decl_mask) {
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
  case TOK_SWITCH: {
    // Stub: skip switch body
    next_token();
    if (lexer.token.type == '(') {
      next_token();
      parse_expr();
      expect(')');
    }
    if (!expect('{'))
      return;
    int depth = 1;
    while (depth > 0) {
      if (lexer.token.type == TOK_EOF)
        return;
      if (lexer.token.type == '{')
        depth++;
      if (lexer.token.type == '}')
        depth--;
      next_token();
    }
    return;
  }
  case TOK_TRY: {
    // Stub: skip try body
    next_token();
    if (!expect('{'))
      return;
    int depth = 1;
    while (depth > 0) {
      if (lexer.token.type == TOK_EOF)
        return;
      if (lexer.token.type == '{')
        depth++;
      if (lexer.token.type == '}')
        depth--;
      next_token();
    }
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
      name = lexer.token.u.ident.atom;
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

void RegParseState::parse_block() {
  expect('{');
  if (lexer.token.type != '}') {
    push_enter_scope();
    for (;;) {
      parse_statement_or_decl(1 /* DECL_MASK_ALL */);
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
  emit_iAsBx(RegOp::JMP, static_cast<uint8_t>(result.reg), 0); // placeholder
  free_temp();
  if (lexer.token.type == ';')
    next_token();
}

void RegParseState::parse_while_statement() {
  BlockEnv be;
  int label_cont  = new_label();
  int label_break = new_label();

  cur_func->push_break(&be, kAtomNull, label_break, label_cont);

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

  cur_func->push_break(&be, kAtomNull, label_break, label_cont);

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
  int tok = lexer.token.type;
  if (tok != ';') {
    if (tok == TOK_VAR || tok == TOK_LET || tok == TOK_CONST) {
      next_token();
      parse_var_decls(tok);
    } else {
      RegSlot r = parse_assign_expr2(0);
      free_temp();
    }
    cur_func->close_scopes(cur_func->scope_level, block_scope_level);
  }
  expect(';');

  // ── Labels ──
  int label_test  = new_label();
  int label_cont  = new_label();
  int label_body  = new_label();
  int label_break = new_label();

  BlockEnv be;
  cur_func->push_break(&be, kAtomNull, label_break, label_cont);

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
    int body_end    = (int)cur_func->instructions.size();
    int update_size = body_start - update_start;

    // Copy update instructions to end
    if (update_size > 0) {
      cur_func->instructions.insert(cur_func->instructions.end(), cur_func->instructions.begin() + update_start,
                                    cur_func->instructions.begin() + body_start);

      // Fill original positions with NOP
      for (int i = 0; i < update_size; i++)
        cur_func->instructions[static_cast<size_t>(update_start + i)] = Instruction::iABx(static_cast<uint8_t>(RegOp::NOP), 0, 0).raw;
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

  FunctionDef *fd = cur_func;
  int scope       = fd->scope_level;
  BlockEnv *top   = fd->top_break;

  while (top) {
    fd->close_scopes(scope, top->scope_level);
    scope = top->scope_level;
    if (is_cont && top->label_cont >= 0) {
      fd->emit_jump(RegOp::JMP, top->label_cont, 0);
      goto done;
    }
    if (!is_cont && top->label_break >= 0) {
      fd->emit_jump(RegOp::JMP, top->label_break, 0);
      goto done;
    }
    top = top->prev;
  }
  // No matching target — fall through silently (error would be better)

done:
  if (lexer.token.type == ';')
    next_token();
}

void RegParseState::parse_var_decls(int decl_tok) {
  for (;;) {
    if (lexer.token.type != TOK_IDENT)
      return;
    Atom name = lexer.token.u.ident.atom;
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
      Atom arg_name = lexer.token.u.ident.atom;
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
    int cpool_idx        = parent_fd->cpool_add(Value::func_bytecode(nullptr));
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

FunctionBytecode *lower_reg(FunctionDef *fd, Context *ctx) {
  auto *b        = new FunctionBytecode();
  b->ref_count   = 1;
  b->gc_obj_type = GCObjType::function_bytecode;
  b->realm       = ctx;

  // Lower child functions first
  for (auto *child : fd->children) {
    FunctionBytecode *child_b = lower_reg(child, ctx);

    // Replace the placeholder in cpool
    for (size_t i = 0; i < fd->cpool.size(); i++) {
      if (fd->cpool[i].is_func_bytecode() && fd->cpool[i].as<FunctionBytecode>() == nullptr) {
        fd->cpool[i] = Value::func_bytecode(child_b);
        break;
      }
    }
  }

  // ── Resolve labels ───────────────────────────────────────────────────────

  for (auto &p : fd->patches) {
    int target   = fd->label_slots[static_cast<size_t>(p.label_id)].pos;
    int current  = p.instr_idx;
    int offset   = target - current;
    uint32_t raw = fd->instructions[static_cast<size_t>(p.instr_idx)];
    Instruction instr{raw};
    RegOp op                                           = static_cast<RegOp>(instr.opcode());
    fd->instructions[static_cast<size_t>(p.instr_idx)] = Instruction::iAsBx(static_cast<uint8_t>(op), instr.a(), static_cast<int16_t>(offset)).raw;
  }

  // ── Copy instructions ─────────────────────────────────────────────────────
  b->instr_count   = static_cast<int>(fd->instructions.size());
  b->byte_code_buf = new uint8_t[static_cast<size_t>(b->instr_count) * 4];
  std::memcpy(b->byte_code_buf, fd->instructions.data(), static_cast<size_t>(b->instr_count) * 4);
  b->byte_code_len = b->instr_count * 4;

  // Copy constant pool
  b->cpool_count = static_cast<int>(fd->cpool.size());
  b->cpool       = new Value[static_cast<size_t>(b->cpool_count)];
  for (int i = 0; i < b->cpool_count; i++)
    b->cpool[i] = fd->cpool[static_cast<size_t>(i)];

  b->arg_count     = static_cast<uint16_t>(fd->arg_count);
  b->var_count     = static_cast<uint16_t>(fd->var_count);
  b->var_ref_count = static_cast<uint16_t>(fd->var_ref_count);
  b->func_name     = fd->func_name;
  b->reg_count     = static_cast<uint16_t>(fd->alloc.total());
  b->stack_size    = b->reg_count; // compat: reuse stack_size for reg_count

  b->js_mode = fd->js_mode;
  if (fd->has_prototype)
    b->flags1 |= 0x01;
  if (fd->func_kind != FunctionKind::normal)
    b->flags1 |= (static_cast<uint8_t>(fd->func_kind) << 4);

  // Closure vars
  b->closure_var_count = static_cast<int>(fd->closure_var.size());
  if (b->closure_var_count > 0) {
    b->closure_var = new ClosureVar[static_cast<size_t>(b->closure_var_count)];
    for (int i = 0; i < b->closure_var_count; i++)
      b->closure_var[i] = fd->closure_var[static_cast<size_t>(i)];
  }

  // Var defs (merged args + vars)
  int total_defs = fd->arg_count + fd->var_count;
  b->var_count   = static_cast<uint16_t>(total_defs);
  if (total_defs > 0) {
    b->vardefs = new BytecodeVarDef[static_cast<size_t>(total_defs)];
    for (int i = 0; i < fd->arg_count; i++) {
      auto &vd                  = fd->args[static_cast<size_t>(i)];
      b->vardefs[i].var_name    = vd.var_name;
      b->vardefs[i].scope_next  = vd.scope_next;
      b->vardefs[i].flags       = 0;
      b->vardefs[i].var_ref_idx = 0;
    }
    int base = fd->arg_count;
    for (int i = 0; i < fd->var_count; i++) {
      auto &vd                        = fd->vars[static_cast<size_t>(i)];
      b->vardefs[base + i].var_name   = vd.var_name;
      b->vardefs[base + i].scope_next = vd.scope_next;
      b->vardefs[base + i].flags      = 0;
      b->vardefs[base + i].set_is_const(vd.is_const);
      b->vardefs[base + i].set_is_lexical(vd.is_lexical);
      b->vardefs[base + i].set_is_captured(vd.is_captured);
      b->vardefs[base + i].set_var_kind(vd.var_kind);
      b->vardefs[base + i].var_ref_idx = 0;
    }
  }

  return b;
}

} // namespace qjsp
