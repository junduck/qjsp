#include "qjsp/parser.hpp"
#include "qjsp/context.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace qjsp {

// ─── OpCode info table ──────────────────────────────────────────────────────

const OpCodeInfo kOpInfo[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) {size, n_pop, n_push, OP_FMT_##f},
#include "qjsp/quickjs-opcode.h"
#undef DEF
#undef FMT
};

// ─── Precedence table ───────────────────────────────────────────────────────

// Operator precedence levels (higher = tighter binding).
// Follows ESTree / QuickJS conventions.
enum Precedence : int {
  PREC_COMMA = 1,
  PREC_ASSIGN = 2,
  PREC_COND = 3,
  PREC_LOGICAL_OR = 4,
  PREC_LOGICAL_AND = 5,
  PREC_BIT_OR = 6,
  PREC_BIT_XOR = 7,
  PREC_BIT_AND = 8,
  PREC_EQ = 9,
  PREC_COMPARE = 10,
  PREC_SHIFT = 11,
  PREC_ADD = 12,
  PREC_MULT = 13,
  PREC_POW = 14,
  PREC_UNARY = 15,
  PREC_POSTFIX = 16,
  PREC_CALL = 17,
  PREC_MEMBER = 18,
  PREC_PRIMARY = 19,
};

static int binary_precedence(int tok) {
  switch (tok) {
  case '*': case '/': case '%': return PREC_MULT;
  case '+': case '-': return PREC_ADD;
  case TOK_SHL: case TOK_SAR: case TOK_SHR: return PREC_SHIFT;
  case '<': case '>': case TOK_LTE: case TOK_GTE:
  case TOK_INSTANCEOF: case TOK_IN: return PREC_COMPARE;
  case TOK_EQ: case TOK_NEQ:
  case TOK_STRICT_EQ: case TOK_STRICT_NEQ: return PREC_EQ;
  case '&': return PREC_BIT_AND;
  case '^': return PREC_BIT_XOR;
  case '|': return PREC_BIT_OR;
  case TOK_LAND: return PREC_LOGICAL_AND;
  case TOK_LOR: return PREC_LOGICAL_OR;
  case TOK_POW: return PREC_POW;
  case TOK_DOUBLE_QUESTION_MARK: return PREC_LOGICAL_OR;
  default: return 0;
  }
}

static uint8_t binop_to_opcode(int tok) {
  switch (tok) {
  case '*': return OP_mul;
  case '/': return OP_div;
  case '%': return OP_mod;
  case '+': return OP_add;
  case '-': return OP_sub;
  case TOK_SHL: return OP_shl;
  case TOK_SAR: return OP_sar;
  case TOK_SHR: return OP_shr;
  case '<': return OP_lt;
  case '>': return OP_gt;
  case TOK_LTE: return OP_lte;
  case TOK_GTE: return OP_gte;
  case TOK_INSTANCEOF: return OP_instanceof;
  case TOK_IN: return OP_in;
  case TOK_EQ: return OP_eq;
  case TOK_NEQ: return OP_neq;
  case TOK_STRICT_EQ: return OP_strict_eq;
  case TOK_STRICT_NEQ: return OP_strict_neq;
  case '&': return OP_and;
  case '^': return OP_xor;
  case '|': return OP_or;
  case TOK_POW: return OP_pow;
  default: return OP_nop;
  }
}

// ─── FunctionDef emitter ────────────────────────────────────────────────────

void FunctionDef::emit_u8(uint8_t v) { byte_code.push_back(v); }
void FunctionDef::emit_u16(uint16_t v) {
  byte_code.push_back(static_cast<uint8_t>(v));
  byte_code.push_back(static_cast<uint8_t>(v >> 8));
}
void FunctionDef::emit_u32(uint32_t v) {
  byte_code.push_back(static_cast<uint8_t>(v));
  byte_code.push_back(static_cast<uint8_t>(v >> 8));
  byte_code.push_back(static_cast<uint8_t>(v >> 16));
  byte_code.push_back(static_cast<uint8_t>(v >> 24));
}

void FunctionDef::emit_atom(Atom a) { emit_u32(static_cast<uint32_t>(a)); }

void FunctionDef::emit_op(uint8_t op) {
  last_opcode_pos = static_cast<int>(byte_code.size());
  byte_code.push_back(op);
}

uint8_t FunctionDef::prev_opcode() const {
  if (last_opcode_pos < 0 || byte_code_has_error) return OP_invalid;
  return byte_code[static_cast<size_t>(last_opcode_pos)];
}

bool FunctionDef::is_live_code() const {
  switch (prev_opcode()) {
  case OP_tail_call: case OP_tail_call_method:
  case OP_return: case OP_return_undef: case OP_return_async:
  case OP_throw: case OP_throw_error:
  case OP_goto: case OP_ret:
    return false;
  default: return true;
  }
}

int FunctionDef::new_label() {
  int idx = label_count++;
  label_slots.emplace_back();
  return idx;
}

int FunctionDef::emit_label(int label) {
  if (label >= 0) {
    emit_op(OP_label);
    emit_u32(static_cast<uint32_t>(label));
    label_slots[static_cast<size_t>(label)].pos =
        static_cast<int>(byte_code.size());
    return static_cast<int>(byte_code.size()) - 4;
  }
  return -1;
}

int FunctionDef::emit_goto(uint8_t opcode, int label) {
  if (is_live_code()) {
    if (label < 0) label = new_label();
    emit_op(opcode);
    emit_u32(static_cast<uint32_t>(label));
    label_slots[static_cast<size_t>(label)].ref_count++;
    return label;
  }
  return -1;
}

int FunctionDef::cpool_add(Value val) {
  cpool.push_back(val);
  return static_cast<int>(cpool.size()) - 1;
}

// ─── FunctionDef variable helpers ───────────────────────────────────────────

int FunctionDef::find_arg(Atom name) {
  for (size_t i = static_cast<size_t>(arg_count); i-- > 0;)
    if (args[i].var_name == name)
      return static_cast<int>(i) | ARGUMENT_VAR_OFFSET;
  return -1;
}

int FunctionDef::find_var(Atom name) {
  for (int i = var_count; i-- > 0;)
    if (vars[static_cast<size_t>(i)].var_name == name &&
        vars[static_cast<size_t>(i)].scope_level == 0)
      return i;
  return find_arg(name);
}

VarDef *FunctionDef::find_scope_var(Atom name, int scope) {
  int idx = scopes[static_cast<size_t>(scope)].first;
  while (idx >= 0) {
    VarDef &vd = vars[static_cast<size_t>(idx)];
    if (vd.scope_level != scope) break;
    if (vd.var_name == name) return &vd;
    idx = vd.scope_next;
  }
  return nullptr;
}

int FunctionDef::add_var(Atom name) {
  if (var_count >= JS_MAX_LOCAL_VARS) return -1;
  int idx = var_count++;
  vars.emplace_back();
  vars[static_cast<size_t>(idx)].var_name = name;
  return idx;
}

int FunctionDef::add_arg(Atom name) {
  if (arg_count >= JS_MAX_LOCAL_VARS) return -1;
  int idx = arg_count++;
  args.emplace_back();
  args[static_cast<size_t>(idx)].var_name = name;
  return idx;
}

// ─── FunctionDef scope helpers ──────────────────────────────────────────────

static int get_first_lexical_var(FunctionDef *fd, int scope) {
  while (scope >= 0) {
    int idx = fd->scopes[static_cast<size_t>(scope)].first;
    if (idx >= 0) return idx;
    scope = fd->scopes[static_cast<size_t>(scope)].parent;
  }
  return -1;
}

int FunctionDef::push_scope() {
  int scope = static_cast<int>(scopes.size());
  scopes.emplace_back();
  scopes[static_cast<size_t>(scope)].parent = scope_level;
  scopes[static_cast<size_t>(scope)].first = scope_first;
  emit_op(OP_enter_scope);
  emit_u16(static_cast<uint16_t>(scope));
  scope_level = scope;
  return scope;
}

void FunctionDef::pop_scope() {
  int scope = scope_level;
  emit_op(OP_leave_scope);
  emit_u16(static_cast<uint16_t>(scope));
  scope_level = scopes[static_cast<size_t>(scope)].parent;
  scope_first = get_first_lexical_var(this, scope_level);
}

void FunctionDef::close_scopes(int scope, int scope_stop) {
  while (scope > scope_stop) {
    emit_op(OP_leave_scope);
    emit_u16(static_cast<uint16_t>(scope));
    scope = scopes[static_cast<size_t>(scope)].parent;
  }
}

// ─── FunctionDef break/continue ─────────────────────────────────────────────

void FunctionDef::push_break(BlockEnv *be, Atom label, int lbreak, int lcont,
                             int drops) {
  be->prev = top_break;
  top_break = be;
  be->label_name = label;
  be->label_break = lbreak;
  be->label_cont = lcont;
  be->drop_count = drops;
  be->label_finally = -1;
  be->scope_level = scope_level;
  be->has_iterator = false;
  be->is_regular_stmt = false;
}

void FunctionDef::pop_break() {
  top_break = top_break->prev;
}

// ─── ParseState ─────────────────────────────────────────────────────────────

void ParseState::init(const char *source, const char *filename) {
  lexer.init(rt, filename, reinterpret_cast<const uint8_t *>(source),
             std::strlen(source));
}

FunctionDef *ParseState::new_function_def(bool is_eval, bool is_func_expr,
                                          const uint8_t *source_ptr) {
  auto *fd = new FunctionDef(rt);
  fd->parent = cur_func;
  fd->is_eval = is_eval;
  fd->is_func_expr = is_func_expr;
  if (cur_func) {
    fd->js_mode = cur_func->js_mode;
    fd->parent_scope_level = cur_func->scope_level;
  }
  fd->filename = rt->atom_to_string(static_cast<Atom>(AtomEnum::fileName))
                     ? rt->intern(
                           String::create(lexer.filename ? lexer.filename : "<eval>"))
                     : kAtomNull;
  fd->last_opcode_pos = -1;
  fd->last_opcode_source_ptr = source_ptr;
  return fd;
}

bool ParseState::expect(int tok) {
  if (lexer.token.type != tok) {
    // For now just fail silently; proper error reporting later.
    return false;
  }
  return next_token();
}

bool ParseState::emit_push_const(Value val, bool as_atom) {
  if (val.is_string() && as_atom) {
    auto *s = val.as<String>();
    // Create atom from string value
    Atom atom = rt->intern(s);
    if (atom != kAtomNull) {
      emit_op(OP_push_atom_value);
      emit_u32(static_cast<uint32_t>(atom));
      return true;
    }
  }
  int idx = cpool_add(val);
  if (idx < 0) return false;
  emit_op(OP_push_const);
  emit_u32(static_cast<uint32_t>(idx));
  return true;
}

void ParseState::emit_return(bool has_val) {
  if (has_val)
    emit_op(OP_return);
  else
    emit_op(OP_return_undef);
}

void ParseState::push_enter_scope() { cur_func->push_scope(); }
void ParseState::pop_leave_scope() { cur_func->pop_scope(); }

void ParseState::set_eval_ret_undefined() {
  if (cur_func->is_eval && cur_func->eval_ret_idx >= 0)
    emit_op(OP_undefined);
}

// ─── Variable definition ────────────────────────────────────────────────────

bool ParseState::js_define_var(Atom name, int tok) {
  FunctionDef *fd = cur_func;
  switch (tok) {
  case TOK_LET:
    if (fd->find_scope_var(name, fd->scope_level)) return false;
    {
      int idx = fd->add_var(name); if (idx < 0) return false;
      VarDef &vd = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind = static_cast<uint8_t>(VarDefKind::let);
      vd.scope_level = fd->scope_level;
      vd.scope_next = fd->scope_first;
      vd.is_lexical = true;
      fd->scopes[static_cast<size_t>(fd->scope_level)].first = idx;
      fd->scope_first = idx;
    }
    return true;
  case TOK_CONST:
    if (fd->find_scope_var(name, fd->scope_level)) return false;
    {
      int idx = fd->add_var(name); if (idx < 0) return false;
      VarDef &vd = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind = static_cast<uint8_t>(VarDefKind::const_);
      vd.scope_level = fd->scope_level;
      vd.scope_next = fd->scope_first;
      vd.is_lexical = true;
      vd.is_const = true;
      fd->scopes[static_cast<size_t>(fd->scope_level)].first = idx;
      fd->scope_first = idx;
    }
    return true;
  case TOK_VAR:
    if (!fd->find_var(name)) {
      int idx = fd->add_var(name); if (idx < 0) return false;
      VarDef &vd = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind = static_cast<uint8_t>(VarDefKind::var_);
      vd.scope_level = 0; // function-scoped
      vd.scope_next = -1; // not scope-chained
    }
    return true;
  default: return false;
  }
}

// ─── is_label / is_let ──────────────────────────────────────────────────────

bool ParseState::is_label() {
  return lexer.token.type == TOK_IDENT &&
         !lexer.token.u.ident.is_reserved &&
         lexer.peek_token(false) == ':';
}

int ParseState::js_is_let(int /*decl_mask*/) {
  if (lexer.token.type == TOK_LET)
    return 1;
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Expression parser
// ═══════════════════════════════════════════════════════════════════════════

// ─── Primary expression ─────────────────────────────────────────────────────

bool ParseState::parse_primary_expr() {
  int tok = lexer.token.type;

  switch (tok) {
  case TOK_NULL:
    emit_op(OP_null);
    return next_token();

  case TOK_FALSE:
    emit_op(OP_push_false);
    return next_token();

  case TOK_TRUE:
    emit_op(OP_push_true);
    return next_token();

  case TOK_NUMBER: {
    double val = lexer.token.u.num.val;
    // If integral, use push_i32; otherwise push_const
    if (val == static_cast<double>(static_cast<int32_t>(val)) &&
        val >= -2147483648.0 && val <= 2147483647.0) {
      emit_op(OP_push_i32);
      emit_u32(static_cast<uint32_t>(static_cast<int32_t>(val)));
    } else {
      emit_push_const(Value::float64(val), false);
    }
    return next_token();
  }

  case TOK_STRING: {
    auto *s = String::create(
        std::string_view{lexer.token.u.str.str, lexer.token.u.str.len});
    emit_push_const(Value::string(s), true);
    return next_token();
  }

  case TOK_IDENT: {
    Atom atom = lexer.token.u.ident.atom;
    if (!next_token()) return false;
    // Emit a scope variable lookup. For now, always use scope_get_var.
    emit_op(OP_scope_get_var);
    emit_atom(atom);
    emit_u16(static_cast<uint16_t>(cur_func->scope_level));
    return true;
  }

  case '(': {
    if (!next_token()) return false;
    if (!parse_expr()) return false;
    return expect(')');
  }

  case '{':
    return parse_object_literal();

  case '[':
    return parse_array_literal();

  case TOK_FUNCTION: {
    Atom name = kAtomNull;
    const uint8_t *ptr = lexer.token.ptr;
    if (!next_token()) return false;
    if (lexer.token.type == TOK_IDENT) {
      name = lexer.token.u.ident.atom;
      if (!next_token()) return false;
    }
    auto *fd = parse_function_decl(PARSE_FUNC_EXPR, FunctionKind::normal,
                                   name, ptr, false);
    if (!fd) return false;
    return true;
  }

  default:
    return false;
  }
}

// ─── Object literal ────────────────────────────────────────────────────

bool ParseState::parse_object_literal() {
  if (!next_token()) return false;

  emit_op(OP_object);

  while (lexer.token.type != '}') {
    Atom name = kAtomNull;

    // Spread: { ...expr }
    if (lexer.token.type == TOK_ELLIPSIS) {
      if (!next_token()) return false;
      if (!parse_assign_expr()) return false;
      emit_op(OP_null); // dummy excludeList
      emit_op(OP_copy_data_properties);
      emit_u8(2 | (1 << 2) | (0 << 5));
      emit_op(OP_drop); // pop excludeList
      emit_op(OP_drop); // pop src object
      goto obj_next;
    }

    // Property name: ident, string, number, or [
    if (lexer.token.type == TOK_IDENT || is_keyword(lexer.token.type)) {
      name = lexer.token.u.ident.atom;
      if (!next_token()) return false;
      // Shorthand: { x } when no ':' follows
      if (lexer.token.type != ':' && lexer.token.type != '(') {
        emit_op(OP_scope_get_var);
        emit_atom(name);
        emit_u16(static_cast<uint16_t>(cur_func->scope_level));
        emit_op(OP_define_field);
        emit_atom(name);
        goto obj_next;
      }
      // Method shorthand: { foo() { } }
      if (lexer.token.type == '(') {
        // Simplified: parse as function expression, define method
        auto *fd = parse_function_decl(PARSE_FUNC_METHOD,
                                       FunctionKind::normal, kAtomNull,
                                       lexer.token.ptr, false);
        if (!fd) return false;
        emit_op(OP_define_method);
        emit_atom(name);
        emit_u8(OP_DEFINE_METHOD_METHOD | OP_DEFINE_METHOD_ENUMERABLE);
        goto obj_next;
      }
    } else if (lexer.token.type == TOK_STRING) {
      // String key → convert to atom
      auto sv = std::string_view{lexer.token.u.str.str, lexer.token.u.str.len};
      auto *s = String::create(sv);
      if (!s) return false;
      name = rt->intern(s);
      if (!next_token()) return false;
    } else if (lexer.token.type == TOK_NUMBER) {
      // Number key → convert to atom
      double d = lexer.token.u.num.val;
      char buf[32];
      snprintf(buf, sizeof(buf), "%.15g", d);
      auto *s = String::create(buf);
      if (!s) return false;
      name = rt->intern(s);
      if (!next_token()) return false;
    } else if (lexer.token.type == '[') {
      // Computed property name — skip for now
      if (!next_token()) return false;
      if (!parse_assign_expr()) return false;
      if (!expect(']')) return false;
      name = kAtomNull; // computed
    } else {
      return false;
    }

    // Regular property: name : value
    if (name == kAtomNull) {
      // Computed: [expr] : value
      emit_op(OP_to_propkey);
      if (!expect(':')) return false;
      if (!parse_assign_expr()) return false;
      emit_op(OP_define_array_el);
      emit_op(OP_drop);
    } else {
      if (!expect(':')) return false;
      if (!parse_assign_expr()) return false;
      if (name == rt->intern(String::create("__proto__"))) {
        emit_op(OP_set_proto);
      } else {
        emit_op(OP_define_field);
        emit_atom(name);
      }
    }

  obj_next:
    if (lexer.token.type != ',') break;
    if (!next_token()) return false;
  }
  return expect('}');
}

// ─── Array literal ─────────────────────────────────────────────────────

bool ParseState::parse_array_literal() {
  if (!next_token()) return false;

  uint32_t idx = 0;
  // Small arrays: stack-allocated
  while (lexer.token.type != ']' && idx < 32) {
    if (lexer.token.type == ',' || lexer.token.type == TOK_ELLIPSIS) break;
    if (!parse_assign_expr()) return false;
    idx++;
    if (lexer.token.type == ',') {
      if (!next_token()) return false;
    } else if (lexer.token.type != ']') {
      // Unexpected token after element
      return false;
    }
  }
  emit_op(OP_array_from);
  emit_u16(static_cast<uint16_t>(idx));

  // Larger arrays / holes
  bool need_length = false;
  while (lexer.token.type != ']' && idx < 0x7fffffff) {
    if (lexer.token.type == TOK_ELLIPSIS) {
      // Spread — simplified: just parse and append
      if (!next_token()) return false;
      if (!parse_assign_expr()) return false;
      emit_op(OP_append);
    } else {
      need_length = true;
      if (lexer.token.type != ',') {
        if (!parse_assign_expr()) return false;
        emit_op(OP_define_field);
        emit_u32(idx);
        need_length = false;
      }
      emit_op(OP_inc);
    }
    idx++;
    if (lexer.token.type != ',') break;
    if (!next_token()) return false;
  }

  if (lexer.token.type == ']') {
    if (need_length) {
      emit_op(OP_dup);
      emit_op(OP_push_i32);
      emit_u32(idx);
      emit_op(OP_put_field);
      emit_atom(rt->intern(String::create("length")));
    }
    return next_token();
  }

  // Huge arrays with dynamic index
  emit_op(OP_push_i32);
  emit_u32(idx);

  while (lexer.token.type != ']') {
    if (lexer.token.type == TOK_ELLIPSIS) {
      if (!next_token()) return false;
      if (!parse_assign_expr()) return false;
      emit_op(OP_append);
    } else {
      need_length = true;
      if (lexer.token.type != ',') {
        if (!parse_assign_expr()) return false;
        emit_op(OP_define_array_el);
        need_length = false;
      }
      emit_op(OP_inc);
    }
    if (lexer.token.type != ',') break;
    if (!next_token()) return false;
  }

  if (need_length) {
    emit_op(OP_dup1);
    emit_op(OP_put_field);
    emit_atom(rt->intern(String::create("length")));
  } else {
    emit_op(OP_drop);
  }
  return expect(']');
}

// ─── Unary expression ───────────────────────────────────────────────────────

bool ParseState::parse_unary(int flags) {
  int tok = lexer.token.type;

  switch (tok) {
  case '+': if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_plus);
    return true;

  case '-': if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_neg);
    return true;

  case '!': if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_lnot);
    return true;

  case '~': if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_not);
    return true;

  case TOK_TYPEOF:
    if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_typeof);
    return true;

  case TOK_VOID:
    if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_undefined); // void → always undefined
    return true;

  case TOK_DELETE:
    if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_undefined); // simplified
    return true;

  case TOK_INC: if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_inc);
    return true;

  case TOK_DEC: if (!next_token()) return false;
    if (!parse_unary(flags)) return false;
    emit_op(OP_dec);
    return true;

  default:
    return parse_postfix_expr(flags);
  }
}

// ─── Postfix expression ─────────────────────────────────────────────────────

bool ParseState::parse_postfix_expr(int flags) {
  if (!parse_primary_expr()) return false;

  for (;;) {
    int tok = lexer.token.type;

    if ((flags & PF_POSTFIX_CALL) && tok == '(') {
      if (!next_token()) return false;
      uint16_t argc = 0;
      if (lexer.token.type != ')') {
        for (;;) {
          if (!parse_assign_expr()) return false;
          argc++;
          if (lexer.token.type == ')') break;
          if (lexer.token.type != ',') return false;
          if (!next_token()) return false;
        }
      }
      emit_op(OP_call);
      emit_u16(argc);
      if (!next_token()) return false;
      continue;
    }

    if (tok == '[') {
      if (!next_token()) return false;
      if (!parse_expr()) return false;
      if (!expect(']')) return false;
      emit_op(OP_get_array_el);
      continue;
    }

    if (tok == '.') {
      if (!next_token()) return false;
      if (lexer.token.type != TOK_IDENT) return false;
      Atom atom = lexer.token.u.ident.atom;
      if (!next_token()) return false;
      emit_op(OP_get_field);
      emit_atom(atom);
      continue;
    }

    if (tok == TOK_INC) {
      if (!next_token()) return false;
      emit_op(OP_post_inc);
      continue;
    }

    if (tok == TOK_DEC) {
      if (!next_token()) return false;
      emit_op(OP_post_dec);
      continue;
    }

    break;
  }
  return true;
}

// ─── Conditional (ternary) ──────────────────────────────────────────────────

bool ParseState::parse_cond_expr(int flags) {
  if (!parse_postfix_expr(flags | PF_POSTFIX_CALL | PF_POW_ALLOWED))
    return false;

  int tok = lexer.token.type;
  if (tok == TOK_POW) {
    // Right-associative **
    if (flags & PF_POW_FORBIDDEN) return false;
    if (!next_token()) return false;
    if (!parse_unary(flags | PF_POW_FORBIDDEN)) return false;
    emit_op(OP_pow);
    return true;
  }

  if (tok == '?') {
    if (!next_token()) return false;
    int l1 = emit_goto(OP_if_false, -1);
    if (!parse_assign_expr()) return false;
    int l2 = emit_goto(OP_goto, -1);
    emit_label(l1);
    if (!expect(':')) return false;
    if (!parse_assign_expr()) return false;
    emit_label(l2);
  }

  return true;
}

// ─── Arrow function helper ─────────────────────────────────────────────

bool ParseState::parse_arrow_body(Atom single_arg) {
  auto *fd = new_function_def(false, true, lexer.token.ptr);
  if (!fd) return false;

  FunctionDef *parent_fd = cur_func;
  cur_func = fd;
  fd->func_kind = FunctionKind::normal;
  fd->has_this_binding = false;
  fd->arguments_allowed = false;
  fd->has_prototype = false;

  fd->push_scope();
  fd->body_scope = fd->scope_level;

  if (single_arg != kAtomNull) {
    if (fd->add_arg(single_arg) < 0) { cur_func = parent_fd; delete fd; return false; }
  }
  // Multi-param case not handled here — caller must parse params

  fd->in_function_body = true;

  if (lexer.token.type == '{') {
    // Body is a block: () => { return expr; }
    if (!parse_block()) { cur_func = parent_fd; delete fd; return false; }
  } else {
    // Body is an expression: () => expr
    if (!parse_assign_expr()) { cur_func = parent_fd; delete fd; return false; }
    fd->emit_op(OP_return);
  }

  fd->emit_op(OP_return_undef);
  fd->pop_scope();

  int cpool_idx = parent_fd->cpool_add(Value::func_bytecode(nullptr));
  fd->parent_cpool_idx = cpool_idx;
  cur_func = parent_fd;

  parent_fd->emit_op(OP_fclosure);
  parent_fd->emit_u32(static_cast<uint32_t>(cpool_idx));
  return true;
}

// ─── Binary expression (precedence climbing) ────────────────────────────────

// Map compound-assignment token to its corresponding binary opcode.
static uint8_t compound_to_binop(int tok) {
  switch (tok) {
  case TOK_MUL_ASSIGN: return OP_mul;
  case TOK_DIV_ASSIGN: return OP_div;
  case TOK_MOD_ASSIGN: return OP_mod;
  case TOK_PLUS_ASSIGN: return OP_add;
  case TOK_MINUS_ASSIGN: return OP_sub;
  case TOK_SHL_ASSIGN: return OP_shl;
  case TOK_SAR_ASSIGN: return OP_sar;
  case TOK_SHR_ASSIGN: return OP_shr;
  case TOK_AND_ASSIGN: return OP_and;
  case TOK_XOR_ASSIGN: return OP_xor;
  case TOK_OR_ASSIGN: return OP_or;
  case TOK_POW_ASSIGN: return OP_pow;
  default: return OP_nop;
  }
}

/// After parsing an expression that may be an lvalue (identifier, field access,
/// array access), detect the lvalue type from the last opcode, rewind the
/// getter opcodes, and return the information needed to emit a setter.
/// Returns true if the previous opcode sequence is a valid lvalue.
static bool get_lvalue(FunctionDef *fd, uint8_t &popcode, Atom &name,
                       uint16_t &scope, int &depth) {
  uint8_t op = fd->prev_opcode();
  auto &bc = fd->byte_code;
  auto pos = static_cast<size_t>(fd->last_opcode_pos);

  switch (op) {
  case OP_scope_get_var: {
    // Layout: OP_scope_get_var (1) + atom (4) + u16 scope (2) = 7 bytes
    name = static_cast<Atom>((static_cast<uint32_t>(bc[pos + 1])) |
                             (static_cast<uint32_t>(bc[pos + 2]) << 8) |
                             (static_cast<uint32_t>(bc[pos + 3]) << 16) |
                             (static_cast<uint32_t>(bc[pos + 4]) << 24));
    scope = static_cast<uint16_t>(bc[pos + 5] |
                                  (static_cast<uint16_t>(bc[pos + 6]) << 8));
    depth = 0;
    break;
  }
  case OP_get_field: {
    // Layout: OP_get_field (1) + atom (4) = 5 bytes
    name = static_cast<Atom>((static_cast<uint32_t>(bc[pos + 1])) |
                             (static_cast<uint32_t>(bc[pos + 2]) << 8) |
                             (static_cast<uint32_t>(bc[pos + 3]) << 16) |
                             (static_cast<uint32_t>(bc[pos + 4]) << 24));
    scope = 0;
    depth = 1;
    break;
  }
  case OP_get_array_el:
    name = kAtomNull;
    scope = 0;
    depth = 2;
    break;
  default:
    return false; // not a valid lvalue
  }

  // Rewind bytecode: remove the getter opcode and its operands
  popcode = op;
  bc.resize(pos);
  fd->last_opcode_pos = -1;
  return true;
}

/// Emit the store opcode corresponding to the rewinded getter.
static void put_lvalue(FunctionDef *fd, uint8_t popcode, Atom name,
                       uint16_t scope) {
  switch (popcode) {
  case OP_scope_get_var:
    fd->emit_op(OP_scope_put_var);
    fd->emit_atom(name);
    fd->emit_u16(scope);
    break;
  case OP_get_field:
    fd->emit_op(OP_put_field);
    fd->emit_atom(name);
    break;
  case OP_get_array_el:
    fd->emit_op(OP_put_array_el);
    break;
  default:
    break;
  }
}

bool ParseState::parse_assign_expr2(int flags) {
  // ── Arrow function detection (before parsing LHS) ──
  // `(a, b) => body` or `x => body`
  if (lexer.token.type == TOK_IDENT &&
      !lexer.token.u.ident.is_reserved &&
      peek_token(true) == TOK_ARROW) {
    Atom arg = lexer.token.u.ident.atom;
    if (!next_token()) return false; // skip ident
    if (!next_token()) return false; // skip =>
    return parse_arrow_body(arg);
  }

  if (lexer.token.type == '(') {
    // Check for arrow: `(a, b) => ...`
    // Simplified: just peek for `=>` after params
    // Full implementation would use skip_parens_token
    // For now: defer to normal expression parsing
  }

  if (!parse_cond_expr(flags)) return false;

  int tok = lexer.token.type;

  // Simple assignment: =
  if (tok == '=') {
    uint8_t popcode = 0;
    Atom name = kAtomNull;
    uint16_t scope = 0;
    int depth = 0;
    if (!get_lvalue(cur_func, popcode, name, scope, depth)) return false;
    if (!next_token()) return false;
    if (!parse_assign_expr2(flags)) return false;
    put_lvalue(cur_func, popcode, name, scope);
    return true;
  }

  // Compound assignment: +=, -=, etc.
  if (tok >= TOK_MUL_ASSIGN && tok <= TOK_POW_ASSIGN) {
    uint8_t popcode = 0;
    Atom name = kAtomNull;
    uint16_t scope = 0;
    int depth = 0;
    if (!get_lvalue(cur_func, popcode, name, scope, depth)) return false;
    if (!next_token()) return false;
    if (!parse_assign_expr2(flags)) return false;
    cur_func->emit_op(compound_to_binop(tok));
    put_lvalue(cur_func, popcode, name, scope);
    return true;
  }

  // Logical assignment: &&=, ||=, ??=
  if (tok >= TOK_LAND_ASSIGN && tok <= TOK_DOUBLE_QUESTION_MARK_ASSIGN) {
    uint8_t popcode = 0;
    Atom name = kAtomNull;
    uint16_t scope = 0;
    int depth = 0;
    if (!get_lvalue(cur_func, popcode, name, scope, depth)) return false;
    if (!next_token()) return false;

    int label1 = emit_goto(
        tok == TOK_LOR_ASSIGN ? OP_if_true : OP_if_false, -1);
    emit_op(OP_drop);
    if (!parse_assign_expr2(flags)) return false;
    put_lvalue(cur_func, popcode, name, scope);
    int label2 = emit_goto(OP_goto, -1);
    emit_label(label1);
    emit_op(OP_nip);
    emit_label(label2);
    return true;
  }

  // Binary operators (precedence climbing)
  for (;;) {
    int prec = binary_precedence(tok);
    if (prec == 0) break;

    // && and || are short-circuit
    if (tok == TOK_LAND || tok == TOK_LOR || tok == TOK_DOUBLE_QUESTION_MARK) {
      if (!next_token()) return false;
      int label = emit_goto(tok == TOK_LOR ? OP_if_true : OP_if_false, -1);
      emit_op(OP_drop);
      if (!parse_assign_expr2(flags)) return false;
      emit_label(label);
      tok = lexer.token.type;
      continue;
    }

    // ** is right-associative
    if (tok == TOK_POW) {
      if (!next_token()) return false;
      if (!parse_unary(flags | PF_POW_FORBIDDEN)) return false;
      emit_op(OP_pow);
      tok = lexer.token.type;
      continue;
    }

    if (!next_token()) return false;
    if (!parse_assign_expr2(flags)) return false;

    // Handle 'in' conditionally
    if (tok == TOK_IN && !(flags & PF_IN_ACCEPTED)) return false;

    {
      uint8_t opcode = binop_to_opcode(tok);
      if (opcode == OP_nop) return false;
      emit_op(opcode);
    }
    tok = lexer.token.type;
  }
  return true;
}

bool ParseState::parse_assign_expr() {
  return parse_assign_expr2(PF_IN_ACCEPTED);
}

bool ParseState::parse_expr2(int /*flags*/) {
  bool comma = false;
  for (;;) {
    if (!parse_assign_expr2(PF_IN_ACCEPTED)) return false;
    if (comma) cur_func->last_opcode_pos = -1;
    if (lexer.token.type != ',') break;
    comma = true;
    if (!next_token()) return false;
  }
  return true;
}

bool ParseState::parse_expr() { return parse_expr2(PF_IN_ACCEPTED); }

// ═══════════════════════════════════════════════════════════════════════════
//  Statement parser
// ═══════════════════════════════════════════════════════════════════════════

bool ParseState::parse_statement() { return parse_statement_or_decl(0); }

bool ParseState::parse_statement_or_decl(int decl_mask) {
  int tok = lexer.token.type;

  switch (tok) {
  case '{': return parse_block();
  case TOK_IF: return parse_if_statement();
  case TOK_RETURN: return parse_return_statement();
  case TOK_THROW: return parse_throw_statement();
  case TOK_WHILE: return parse_while_statement();
  case TOK_FOR: return parse_for_statement();
  case TOK_DO: return parse_do_statement();
  case TOK_SWITCH: return parse_switch_statement();
  case TOK_BREAK:
  case TOK_CONTINUE: {
    FunctionDef *fd = cur_func;
    bool is_cont = (tok == TOK_CONTINUE);

    if (!next_token()) return false;

    // Walk up the break stack to find the matching label
    int scope = fd->scope_level;
    BlockEnv *top = fd->top_break;
    while (top) {
      fd->close_scopes(scope, top->scope_level);
      scope = top->scope_level;
      if (is_cont && top->label_cont != -1) {
        fd->emit_goto(OP_goto, top->label_cont);
        goto done_break;
      }
      if (!is_cont && top->label_break != -1) {
        fd->emit_goto(OP_goto, top->label_break);
        goto done_break;
      }
      for (int i = 0; i < top->drop_count; i++)
        fd->emit_op(OP_drop);
      top = top->prev;
    }
    return false; // no matching break/continue target
  done_break:
    // Semicolon
    if (lexer.token.type == ';')
      return next_token();
    return true;
  }
  case TOK_LET:
  case TOK_CONST:
    if (!(decl_mask & DECL_MASK_OTHER))
      return parse_expr_statement();
    // fall through
  case TOK_VAR:
    if (!next_token()) return false;
    if (!parse_var_decls(tok, PF_IN_ACCEPTED)) return false;
    // Semicolon
    if (lexer.token.type == ';') {
      if (!next_token()) return false;
    } else if (lexer.token.type == TOK_EOF || lexer.token.type == '}' ||
               lexer.got_lf) {
      // ASI
    }
    return true;

  case TOK_FUNCTION:
    if (!next_token()) return false;
    // function declaration
    {
      Atom name = kAtomNull;
      if (lexer.token.type == TOK_IDENT) {
        name = lexer.token.u.ident.atom;
        if (!next_token()) return false;
      }
      auto *fd = parse_function_decl(PARSE_FUNC_STATEMENT, FunctionKind::normal,
                                     name, lexer.token.ptr, false);
      if (!fd) return false;
      // Bind function name to variable in current scope
      if (name != kAtomNull) {
        emit_op(OP_scope_put_var_init);
        emit_atom(name);
        emit_u16(static_cast<uint16_t>(cur_func->scope_level));
      }
    }
    return true;

  case ';':
    return next_token();

  default:
    return parse_expr_statement();
  }
}

bool ParseState::parse_block() {
  if (!expect('{')) return false;
  if (lexer.token.type != '}') {
    push_enter_scope();
    for (;;) {
      if (!parse_statement_or_decl(DECL_MASK_ALL)) return false;
      if (lexer.token.type == '}') break;
    }
    pop_leave_scope();
  }
  return next_token();
}

bool ParseState::parse_expr_statement() {
  int tok = lexer.token.type;
  if (tok == TOK_FUNCTION) {
    return false;
  }
  if (!parse_expr()) return false;

  // Semicolon insertion
  if (lexer.token.type == ';') {
    if (!next_token()) return false;
  } else if (lexer.token.type == TOK_EOF || lexer.token.type == '}' ||
             lexer.got_lf) {
    // ASI
  } else {
    return false;
  }
  // Keep value on stack for potential return
  return true;
}

bool ParseState::parse_if_statement() {
  if (!next_token()) return false;

  push_enter_scope();
  set_eval_ret_undefined();

  if (!expect('(')) return false;
  if (!parse_expr()) return false;
  if (!expect(')')) return false;

  int l1 = emit_goto(OP_if_false, -1);
  if (!parse_statement()) return false;

  if (lexer.token.type == TOK_ELSE) {
    int l2 = emit_goto(OP_goto, -1);
    if (!next_token()) return false;
    emit_label(l1);
    if (!parse_statement()) return false;
    l1 = l2;
  }
  emit_label(l1);

  pop_leave_scope();
  return true;
}

bool ParseState::parse_return_statement() {
  if (!next_token()) return false;

  if (lexer.token.type != ';' && lexer.token.type != '}' && !lexer.got_lf) {
    if (!parse_expr()) return false;
    emit_return(true);
  } else {
    emit_return(false);
  }

  // Semicolon
  if (lexer.token.type == ';') {
    if (!next_token()) return false;
  }
  return true;
}

bool ParseState::parse_throw_statement() {
  if (!next_token()) return false;
  if (lexer.got_lf) return false; // no line terminator after throw
  if (!parse_expr()) return false;
  emit_op(OP_throw);

  if (lexer.token.type == ';') return next_token();
  return true;
}

/// Parse var/let/const declarations. Does NOT consume trailing ';'.
bool ParseState::parse_var_decls(int tok, int flags) {
  for (;;) {
    if (lexer.token.type != TOK_IDENT) return false;

    Atom name = lexer.token.u.ident.atom;
    if (!next_token()) return false;

    if (!js_define_var(name, tok)) return false;

    if (lexer.token.type == '=') {
      if (!next_token()) return false;
      if (!parse_assign_expr2(flags)) return false;
      emit_op(OP_scope_put_var);
      emit_atom(name);
      emit_u16(static_cast<uint16_t>(cur_func->scope_level));
    } else if (tok == TOK_CONST) {
      return false; // const must have initializer
    } else if (tok == TOK_LET) {
      emit_op(OP_undefined);
      emit_op(OP_scope_put_var_init);
      emit_atom(name);
      emit_u16(static_cast<uint16_t>(cur_func->scope_level));
    }

    if (lexer.token.type != ',') break;
    if (!next_token()) return false;
  }
  return true;
}

bool ParseState::parse_while_statement() {
  BlockEnv be;
  int label_cont = new_label();
  int label_break = new_label();

  cur_func->push_break(&be, kAtomNull, label_break, label_cont, 0);

  if (!next_token()) return false;
  set_eval_ret_undefined();

  emit_label(label_cont);
  if (!expect('(')) return false;
  if (!parse_expr()) return false;
  if (!expect(')')) return false;

  emit_goto(OP_if_false, label_break);

  if (!parse_statement()) return false;
  emit_goto(OP_goto, label_cont);

  emit_label(label_break);
  cur_func->pop_break();
  return true;
}

bool ParseState::parse_do_statement() {
  BlockEnv be;
  int label_cont = new_label();
  int label_break = new_label();
  int label_body = new_label();

  cur_func->push_break(&be, kAtomNull, label_break, label_cont, 0);

  if (!next_token()) return false;
  emit_label(label_body);
  set_eval_ret_undefined();

  if (!parse_statement()) return false;
  if (!expect(TOK_WHILE)) return false;

  emit_label(label_cont);
  if (!expect('(')) return false;
  if (!parse_expr()) return false;
  if (!expect(')')) return false;

  emit_goto(OP_if_true, label_body);
  emit_label(label_break);

  cur_func->pop_break();

  // Semicolon after do-while
  if (lexer.token.type == ';') return next_token();
  return true;
}

bool ParseState::parse_for_statement() {
  if (!next_token()) return false;
  if (!expect('(')) return false;

  int block_scope_level = cur_func->scope_level;
  push_enter_scope();

  // ── INIT ──
  int tok = lexer.token.type;
  if (tok != ';') {
    // Scan ahead to detect let/const (original: is_let)
    if (tok == TOK_VAR || tok == TOK_LET || tok == TOK_CONST) {
      if (!next_token()) return false;
      if (!parse_var_decls(tok, 0)) return false;
    } else {
      if (!parse_expr2(0)) return false;
      emit_op(OP_drop);
    }
    cur_func->close_scopes(cur_func->scope_level, block_scope_level);
  }
  if (lexer.token.type != ';') return false;
  if (!next_token()) return false;

  // ── Create labels ──
  int label_test = new_label();
  int label_cont = new_label();
  int label_body = new_label();
  int label_break = new_label();

  BlockEnv be;
  cur_func->push_break(&be, kAtomNull, label_break, label_cont, 0);

  // ── COND ──
  if (lexer.token.type == ';') {
    // No test expression — label_test = label_body
    label_test = label_body;
  } else {
    emit_label(label_test);
    if (!parse_expr()) return false;
    emit_goto(OP_if_false, label_break);
  }
  if (lexer.token.type != ';') return false;
  if (!next_token()) return false;

  // ── UPDATE ──
  size_t pos_cont = 0;
  if (lexer.token.type == ')') {
    // No increment expression — continue lands on test
    be.label_cont = label_cont = label_test;
  } else {
    // Emit goto that skips the update on first entry
    emit_goto(OP_goto, label_body);

    // Emit the update code (will be moved after body by OPTIMIZE below)
    pos_cont = cur_func->byte_code.size();
    emit_label(label_cont);

    int saved_pos = cur_func->last_opcode_pos;
    cur_func->last_opcode_pos = -1; // prevent get_lvalue from seeing prev op
    if (!parse_expr()) return false;
    emit_op(OP_drop);
    cur_func->last_opcode_pos = saved_pos;

    if (label_test != label_body)
      emit_goto(OP_goto, label_test);
  }

  if (!expect(')')) return false;

  // ── BODY ──
  size_t pos_body = cur_func->byte_code.size();
  emit_label(label_body);
  if (!parse_statement()) return false;

  cur_func->close_scopes(cur_func->scope_level, block_scope_level);

  // ── Move update code after body (OPTIMIZE) ──
  if (label_test != label_body && label_cont != label_test) {
    size_t chunk_size = pos_body - pos_cont;
    size_t offset = cur_func->byte_code.size() - pos_cont;

    // Append update bytecodes to end of buffer
    cur_func->byte_code.insert(
        cur_func->byte_code.end(),
        cur_func->byte_code.begin() + static_cast<long>(pos_cont),
        cur_func->byte_code.begin() + static_cast<long>(pos_cont) +
            static_cast<long>(chunk_size));

    // Fill original positions with NOPs
    for (size_t i = 0; i < chunk_size; i++)
      cur_func->byte_code[pos_cont + i] = OP_nop;

    // Update last_opcode_pos — the moved code's last goto is now at the end
    cur_func->last_opcode_pos =
        static_cast<int>(cur_func->byte_code.size()) - 5;

    // Relocate labels that were in the moved range
    for (size_t i = static_cast<size_t>(label_cont);
         i < static_cast<size_t>(cur_func->label_count); i++) {
      LabelSlot &ls = cur_func->label_slots[i];
      if (ls.pos >= static_cast<int>(pos_cont) &&
          ls.pos < static_cast<int>(pos_body))
        ls.pos += static_cast<int>(offset);
    }
  } else {
    emit_goto(OP_goto, label_cont);
  }

  emit_label(label_break);
  cur_func->pop_break();
  pop_leave_scope();
  return true;
}

bool ParseState::parse_switch_statement() {
  // Stub: skip past the switch body.
  if (!next_token()) return false;
  if (!expect('(')) return false;
  if (!parse_expr()) return false;
  if (!expect(')')) return false;
  if (!expect('{')) return false;

  int depth = 1;
  while (depth > 0) {
    int tok = lexer.token.type;
    if (tok == TOK_EOF) return false;
    if (tok == '{') depth++;
    if (tok == '}') depth--;
    if (!next_token() && depth > 0) return false;
  }
  return true;
}

bool ParseState::parse_try_statement() {
  // Stub: skip past try/catch/finally.
  if (!next_token()) return false;
  if (!expect('{')) return false;

  // Find matching }
  int depth = 1;
  while (depth > 0) {
    int tok = lexer.token.type;
    if (tok == TOK_EOF) return false;
    if (tok == '{') depth++;
    if (tok == '}') depth--;
    if (!next_token() && depth > 0) return false;
  }
  return true;
}

// ─── Function declaration ───────────────────────────────────────────────────

FunctionDef *ParseState::parse_function_decl(ParseFuncType func_type,
                                             FunctionKind func_kind,
                                             Atom func_name,
                                             const uint8_t *ptr,
                                             bool /*is_export*/) {
  FunctionDef *parent_fd = cur_func;
  auto *fd = new_function_def(false, func_type == PARSE_FUNC_EXPR, ptr);
  if (!fd) return nullptr;

  fd->func_kind = func_kind;
  fd->func_name = func_name;
  fd->has_prototype = (func_type != PARSE_FUNC_ARROW);
  fd->has_this_binding = (func_type != PARSE_FUNC_ARROW);
  fd->arguments_allowed = true;

  cur_func = fd;
  int cpool_idx = -1; // declared early to avoid goto-bypass warning

  // Parse parameters: (a, b, c)
  if (!expect('(')) goto fail;

  fd->push_scope(); // enter body scope
  fd->body_scope = fd->scope_level;

  if (lexer.token.type != ')') {
    for (;;) {
      if (lexer.token.type != TOK_IDENT) goto fail;
      Atom arg_name = lexer.token.u.ident.atom;
      if (!next_token()) goto fail;

      int idx = fd->add_arg(arg_name);
      if (idx < 0) goto fail;

      if (lexer.token.type == ')') break;
      if (lexer.token.type != ',') goto fail;
      if (!next_token()) goto fail;
    }
  }
  if (!expect(')')) goto fail;

  // Parse body
  if (!expect('{')) goto fail;

  fd->in_function_body = true;

  while (lexer.token.type != '}') {
    if (!parse_statement_or_decl(DECL_MASK_ALL)) goto fail;
  }
  if (!next_token()) goto fail;

  fd->emit_op(OP_return_undef);

  // Finalize — pop scope
  fd->pop_scope();

  // Add to constant pool of parent (placeholder, will be resolved during lowering)
  cpool_idx = parent_fd->cpool_add(Value::func_bytecode(nullptr));
  fd->parent_cpool_idx = cpool_idx;

  // Track child for later lowering
  parent_fd->children.push_back(fd);

  cur_func = parent_fd;

  // Emit fclosure in parent bytecode
  parent_fd->emit_op(OP_fclosure);
  parent_fd->emit_u32(static_cast<uint32_t>(cpool_idx));

  return fd;

fail:
  cur_func = parent_fd;
  delete fd;
  return nullptr;
}

// ─── Compilation entry ──────────────────────────────────────────────────────

bool ParseState::compile() {
  auto *fd = new_function_def(true, false, lexer.buf_start);
  if (!fd) return false;

  fd->is_global_var = true;
  fd->is_eval = true;
  fd->has_arguments_binding = true;
  fd->has_this_binding = true;
  fd->in_function_body = true;

  cur_func = fd;
  lexer.next_token();

  // Enter the eval scope
  fd->push_scope();
  fd->body_scope = fd->scope_level;

  // Parse statements until EOF
  while (lexer.token.type != TOK_EOF) {
    if (!parse_statement_or_decl(DECL_MASK_ALL)) return false;
  }

  emit_op(OP_return);
  fd->pop_scope();

  return true;
}

} // namespace qjsp
