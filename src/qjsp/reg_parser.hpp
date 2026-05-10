#pragma once

#include "qjsp/atom.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/lexer.hpp"
#include "qjsp/reg_opcode.hpp"
#include "qjsp/value.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qjsp {

struct Runtime;
struct Context;
struct String;

// ─── RegSlot ────────────────────────────────────────────────────────────────

struct RegSlot {
  int reg;
  RegSlot() : reg(0) {}
  /*implicit*/ RegSlot(int r) : reg(r) {}
  /*implicit*/ RegSlot(uint8_t r) : reg(static_cast<int>(r)) {}
};

constexpr int kUnusedReg = 0xFF;

// ─── LValue — LHS of an assignment ──────────────────────────────────────────

struct LValue {
  enum Kind : uint8_t { LOCAL, ARG, FIELD, ELEM, GLOBAL, UPVAL };
  Kind kind     = LOCAL;
  int obj_reg   = -1;
  Atom prop     = kAtomNull;
  int key_reg   = -1;
  int var_idx   = -1;
  int upval_idx = -1; // for UPVAL kind
};

// ─── RegAlloc — register allocator per function ─────────────────────────────

struct RegAlloc {
  uint32_t arg_count_ = 0;
  uint32_t var_count_ = 0;
  uint32_t next_temp_ = 0;
  uint32_t max_temp_  = 0;

  void init(int ac, int vc) {
    arg_count_ = static_cast<uint32_t>(ac);
    var_count_ = static_cast<uint32_t>(vc);
    next_temp_ = 1 + arg_count_ + var_count_;
    max_temp_  = next_temp_;
  }

  int this_reg() const { return 0; }
  int arg(int i) const { return 1 + i; }
  int var(int i) const { return static_cast<int>(1 + arg_count_ + static_cast<uint32_t>(i)); }
  int alloc() {
    int r = static_cast<int>(next_temp_++);
    if (next_temp_ > max_temp_)
      max_temp_ = next_temp_;
    return r;
  }
  void free_last() { next_temp_--; }
  uint32_t total() const { return max_temp_; }
  void ensure_max(int r) {
    auto ru = static_cast<uint32_t>(r);
    if (ru > max_temp_)
      max_temp_ = ru;
  }
};

// ─── LabelSlot ──────────────────────────────────────────────────────────────

struct LabelSlot {
  int pos       = -1; // instruction index where label is placed
  int ref_count = 0;
};

// ─── VarDef ─────────────────────────────────────────────────────────────────

struct VarDef {
  Atom var_name       = kAtomNull;
  int scope_level     = 0;
  int scope_next      = -1;
  bool is_const       = false;
  bool is_lexical     = false;
  bool is_captured    = false;
  VarDefKind var_kind = VarDefKind::unknown_;
  int func_pool_idx   = -1;
  int reg_index       = -1; // assigned register slot
  int upval_idx       = -1; // upvalue index in enclosing frame (if captured)
};

// ─── VarScope ───────────────────────────────────────────────────────────────

struct VarScope {
  int parent = -1;
  int first  = -1;
};

// ─── BlockEnv ───────────────────────────────────────────────────────────────

struct BlockEnv {
  BlockEnv *prev    = nullptr;
  Atom label_name   = kAtomNull;
  int label_break   = -1;
  int label_cont    = -1;
  int drop_count    = 0;
  int scope_level   = 0;
  bool has_iterator = false;
};

// ─── FunctionDef ────────────────────────────────────────────────────────────

struct FunctionDef {
  Runtime *rt         = nullptr;
  FunctionDef *parent = nullptr;

  // Instructions
  std::vector<uint32_t> instructions;

  // Labels
  std::vector<LabelSlot> label_slots;

  struct PatchEntry {
    int instr_idx;
    int label_id;
  };
  std::vector<PatchEntry> patches;
  int last_instr_index = -1;

  // Constant pool
  std::vector<Value> cpool;

  // Variables
  std::vector<VarDef> vars;
  std::vector<VarDef> args;
  uint16_t arg_count = 0;
  uint16_t var_count = 0;

  // Closures
  std::vector<ClosureVar> closure_var;
  uint16_t var_ref_count = 0;
  uint16_t next_upval    = 0; // next available upvalue index in this frame

  // Scopes
  std::vector<VarScope> scopes;
  int scope_level = -1;
  int scope_first = -1;
  int body_scope  = 0;

  // Break/continue
  BlockEnv *top_break = nullptr;

  // Register allocator
  RegAlloc alloc;

  // Flags
  FunctionKind func_kind = FunctionKind::normal;
  Atom func_name         = kAtomNull;
  uint8_t js_mode        = 0;
  bool has_prototype     = true;
  bool has_this_binding  = true;
  bool arguments_allowed = false;
  bool is_eval           = false;
  bool is_func_expr      = false;
  bool in_function_body  = false;
  bool is_global_var     = false;

  int eval_ret_reg = -1; // register holding implicit return value for eval

  // Parent linkage
  int parent_cpool_idx   = -1;
  int parent_scope_level = 0;

  // Child functions
  std::vector<FunctionDef *> children;

  // Source info
  Atom filename = kAtomNull;
  std::string source;

  FunctionDef(Runtime *r) : rt(r) {}
  ~FunctionDef() { for (auto *c : children) delete c; }

  // ── emitter ───────────────────────────────────────────────────────────

  void emit_iABC(RegOp op, uint8_t a, uint8_t b, uint8_t c);
  void emit_iABx(RegOp op, uint8_t a, uint16_t bx);
  void emit_iAsBx(RegOp op, uint8_t a, int16_t sbx);
  int new_label();
  int emit_label(int id);
  int emit_jump(RegOp op, int label, uint8_t reg_a);

  // ── constant pool ─────────────────────────────────────────────────────

  int cpool_add(Value val);

  // ── variable resolution ───────────────────────────────────────────────

  int find_var(Atom name);
  int find_arg(Atom name);
  int add_var(Atom name);
  int add_arg(Atom name);
  VarDef *find_scope_var(Atom name, int scope);

  // ── closure / upvalue ─────────────────────────────────────────────────

  int capture_var(VarDef *vd);
  bool find_enclosing_var(Atom name, VarDef *&vd, FunctionDef *&owner, int &var_idx, bool &is_arg);
  int resolve_upval(Atom name);

  int first_lexical_var(int scope);
  int push_scope();
  void pop_scope();
  void close_scopes(int scope, int scope_stop);

  // ── break/continue ────────────────────────────────────────────────────

  void push_break(BlockEnv *be, Atom label, int lbreak, int lcont);
  void pop_break();
};

// ─── RegParseState ──────────────────────────────────────────────────────────

struct RegParseState {
  Runtime *rt;
  Context *ctx;
  Lexer lexer;
  FunctionDef *cur_func = nullptr;

  struct TryInfo {
    int finally_label = -1;
    int exc_reg       = -1;
    int scope_level   = -1;
  };
  std::vector<TryInfo> try_stack_;

  RegParseState(Runtime *rt_, Context *ctx_) : rt(rt_), ctx(ctx_) {}

  // ── init ──────────────────────────────────────────────────────────────

  void init(const char *source, const char *filename);
  bool compile();

  // ── token helpers ─────────────────────────────────────────────────────

  bool next_token() { return lexer.next_token(); }
  TokenKind peek_token(bool no_lf) { return lexer.peek_token(no_lf); }
  bool expect(TokenKind tok);

  // ── emitter ───────────────────────────────────────────────────────────

  void emit_iABC(RegOp op, uint8_t a, uint8_t b, uint8_t c) { cur_func->emit_iABC(op, a, b, c); }
  void emit_iABx(RegOp op, uint8_t a, uint16_t bx) { cur_func->emit_iABx(op, a, bx); }
  void emit_iAsBx(RegOp op, uint8_t a, int16_t sbx) { cur_func->emit_iAsBx(op, a, sbx); }
  int new_label() { return cur_func->new_label(); }
  int emit_label(int id) { return cur_func->emit_label(id); }
  int emit_jump(RegOp op, int label, RegSlot cond) { return cur_func->emit_jump(op, label, static_cast<uint8_t>(cond.reg)); }
  int cpool_add(Value v) { return cur_func->cpool_add(v); }

  int alloc_temp() { return cur_func->alloc.alloc(); }
  void free_temp() { cur_func->alloc.free_last(); }

  Atom pending_label = kAtomNull; // set before loop parsing for named labels

  // ── expression parsers ────────────────────────────────────────────────

  RegSlot parse_expr();
  RegSlot parse_assign_expr();
  RegSlot parse_assign_expr2(int flags);
  RegSlot parse_cond_expr(int flags);
  RegSlot parse_binary(int min_prec);
  RegSlot parse_binary_from(RegSlot left, int min_prec);
  RegSlot parse_unary(int flags);
  RegSlot parse_postfix(int flags);
  RegSlot parse_postfix_continue(RegSlot base, int flags);
  RegSlot parse_primary();

  // ── lvalue (for LHS of assignments) ───────────────────────────────────

  LValue parse_lvalue();
  LValue parse_ident_lvalue();
  LValue parse_postfix_lvalue();
  void emit_lvalue_load(LValue lv, RegSlot dst);
  void emit_lvalue_store(LValue lv, RegSlot val);
  int cpool_atom(Atom a); // add atom-string to cpool, return index

  // ── object / array ────────────────────────────────────────────────────

  RegSlot parse_object_literal();
  RegSlot parse_array_literal();

  // ── statement parsers ─────────────────────────────────────────────────

  void parse_statement();
  void parse_statement_or_decl(int decl_mask);
  void parse_block();
  void parse_expr_statement();
  void parse_if_statement();
  void parse_return_statement();
  void parse_throw_statement();
  void parse_while_statement();
  void parse_do_statement();
  void parse_for_statement();
  void parse_break_continue(bool is_cont);
  void parse_var_decls(TokenKind decl_tok);
  void parse_try_statement();
  void parse_switch_statement();

  // ── upvalue emission ──────────────────────────────────────────────────

  RegSlot emit_upval_read(int closure_idx);
  void emit_upval_write(int closure_idx, RegSlot val);

  // ── function parsing ──────────────────────────────────────────────────

  FunctionDef *parse_function_decl(Atom name, bool is_expr, FunctionKind func_kind);

  // ── helpers ───────────────────────────────────────────────────────────

  bool js_define_var(Atom name, TokenKind tok);
  void push_enter_scope() { cur_func->push_scope(); }
  void pop_leave_scope() { cur_func->pop_scope(); }
};

// ─── Expression parse flags ─────────────────────────────────────────────────

constexpr int PF_IN_ACCEPTED  = (1 << 0);
constexpr int PF_POSTFIX_CALL = (1 << 1);

// ─── Operator precedence ────────────────────────────────────────────────────

enum Precedence : int {
  PREC_COMMA       = 1,
  PREC_ASSIGN      = 2,
  PREC_COND        = 3,
  PREC_LOGICAL_OR  = 4,
  PREC_LOGICAL_AND = 5,
  PREC_BIT_OR      = 6,
  PREC_BIT_XOR     = 7,
  PREC_BIT_AND     = 8,
  PREC_EQ          = 9,
  PREC_COMPARE     = 10,
  PREC_SHIFT       = 11,
  PREC_ADD         = 12,
  PREC_MULT        = 13,
  PREC_POW         = 14,
  PREC_UNARY       = 15,
  PREC_POSTFIX     = 16,
  PREC_CALL        = 17,
};

// ─── Lowering ───────────────────────────────────────────────────────────────

FunctionBytecode *lower_reg(FunctionDef *fd, Context *ctx);

// ─── Inline helpers ─────────────────────────────────────────────────────────

inline int binary_precedence(TokenKind tok) {
  switch (tok) {
  case TokenKind::Star:
  case TokenKind::SlashChar:
  case TokenKind::Percent:
    return PREC_MULT;
  case TokenKind::Plus:
  case TokenKind::Minus:
    return PREC_ADD;
  case TokenKind::Shl:
  case TokenKind::Sar:
  case TokenKind::Shr:
    return PREC_SHIFT;
  case TokenKind::Less:
  case TokenKind::Greater:
  case TokenKind::Lte:
  case TokenKind::Gte:
    return PREC_COMPARE;
  case TokenKind::Eq:
  case TokenKind::StrictEq:
  case TokenKind::Neq:
  case TokenKind::StrictNeq:
    return PREC_EQ;
  case TokenKind::Amp:
    return PREC_BIT_AND;
  case TokenKind::Caret:
    return PREC_BIT_XOR;
  case TokenKind::Pipe:
    return PREC_BIT_OR;
  case TokenKind::Land:
    return PREC_LOGICAL_AND;
  case TokenKind::Lor:
    return PREC_LOGICAL_OR;
  case TokenKind::Pow:
    return PREC_POW;
  case TokenKind::DoubleQuestionMark:
    return PREC_LOGICAL_OR;
  default:
    return 0;
  }
}

inline RegOp binop_to_reg(TokenKind tok) {
  switch (tok) {
  case TokenKind::Star:
    return RegOp::MUL;
  case TokenKind::SlashChar:
    return RegOp::DIV;
  case TokenKind::Percent:
    return RegOp::MOD;
  case TokenKind::Plus:
    return RegOp::ADD;
  case TokenKind::Minus:
    return RegOp::SUB;
  case TokenKind::Shl:
    return RegOp::SHL;
  case TokenKind::Sar:
    return RegOp::SAR;
  case TokenKind::Shr:
    return RegOp::SHR;
  case TokenKind::Less:
    return RegOp::LT;
  case TokenKind::Greater:
    return RegOp::GT;
  case TokenKind::Lte:
    return RegOp::LTE;
  case TokenKind::Gte:
    return RegOp::GTE;
  case TokenKind::Eq:
    return RegOp::EQ;
  case TokenKind::Neq:
    return RegOp::NEQ;
  case TokenKind::StrictEq:
    return RegOp::SEQ;
  case TokenKind::StrictNeq:
    return RegOp::SNEQ;
  case TokenKind::Amp:
    return RegOp::AND;
  case TokenKind::Caret:
    return RegOp::XOR;
  case TokenKind::Pipe:
    return RegOp::OR;
  case TokenKind::Pow:
    return RegOp::POW;
  default:
    return RegOp::NOP;
  }
}

inline RegOp compound_to_binop(TokenKind tok) {
  switch (tok) {
  case TokenKind::MulAssign:
    return RegOp::MUL;
  case TokenKind::DivAssign:
    return RegOp::DIV;
  case TokenKind::ModAssign:
    return RegOp::MOD;
  case TokenKind::PlusAssign:
    return RegOp::ADD;
  case TokenKind::MinusAssign:
    return RegOp::SUB;
  case TokenKind::ShlAssign:
    return RegOp::SHL;
  case TokenKind::SarAssign:
    return RegOp::SAR;
  case TokenKind::ShrAssign:
    return RegOp::SHR;
  case TokenKind::AndAssign:
    return RegOp::AND;
  case TokenKind::OrAssign:
    return RegOp::OR;
  case TokenKind::XorAssign:
    return RegOp::XOR;
  case TokenKind::PowAssign:
    return RegOp::POW;
  default:
    return RegOp::NOP;
  }
}

} // namespace qjsp
