#pragma once

#include "qjsp/atom.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/lexer.hpp"
#include "qjsp/reg_opcode.hpp"
#include "qjsp/value.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace qjsp {

struct Engine;
struct StrPrim;

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
  Engine *e_          = nullptr;
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

  FunctionDef(Engine *e) : e_(e) {}
  ~FunctionDef() {
    for (auto *c : children)
      delete c;
  }

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
  Engine *e_;
  Lexer lexer;
  FunctionDef *cur_func = nullptr;

  struct TryInfo {
    int finally_label = -1;
    int exc_reg       = -1;
    int scope_level   = -1;
  };
  std::vector<TryInfo> try_stack_;

  // For-of destructuring tracking from the for-init
  struct ForPatternBind {
    Atom prop;
    int reg;
  }; // prop=var_name for arrays, property key for objects
  struct ForPattern {
    bool is_array  = false;
    bool is_object = false;
    std::vector<ForPatternBind> binds;
  };
  ForPattern for_pattern_;

  RegParseState(Engine *e) : e_(e) {}

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

  // ── Pratt expression parser ────────────────────────────────────────────

  RegSlot parse_expr(int min_prec = 1);             // main Pratt loop
  RegSlot parse_assign_expr();                      // lvalue-based assignment entry
  RegSlot parse_prefix();                           // dispatch on token kind
  RegSlot parse_infix(RegSlot left, TokenKind tok); // dispatch on operator

  // ── lvalue (for LHS of assignments) ───────────────────────────────────

  LValue parse_lvalue();
  LValue parse_ident_lvalue();
  LValue parse_postfix_lvalue();
  void emit_lvalue_load(LValue lv, RegSlot dst);
  void emit_lvalue_store(LValue lv, RegSlot val);
  int cpool_atom(Atom a); // add atom-string to cpool, return index

  // Track the lvalue from the most recent parse_ident (in value context)
  // so that postfix/prefix INC/DEC can write back to the actual variable.
  LValue last_prefix_lvalue_;
  bool has_prefix_lvalue_ = false;

  // Track the object register for method calls (obj.method())
  int last_obj_reg_ = -1;
  bool has_obj_reg_  = false;

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

  // ── unified cover parsing for literals + destructuring ────────────────

  struct CoverProp {
    enum Kind : uint8_t {
      Shorthand, // {x} or [x] — key is the var name
      KeyValue,  // x: expr  — separate key and value
      Computed,  // [expr]: val
      Spread,    // ...expr
      Elision,   // [,] in array patterns
    };
    Kind kind = Shorthand;
    Atom key  = kAtomNull;
    RegSlot value; // expression result (for KeyValue, Computed, Spread)
  };

  bool parse_cover_property(CoverProp &out);
};

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

FunctionBytecode *lower_reg(FunctionDef *fd, Engine *e);

// ─── Operator metadata tables and accessors ─────────────────────────────────

struct BinOpInfo {
  uint8_t prec;
  RegOp opcode;
  bool right_assoc;
};

constexpr size_t kTokenTableSize = 512;

constexpr inline auto kBinOpTable = []() constexpr {
  std::array<BinOpInfo, kTokenTableSize> tab{};
  tab[static_cast<uint16_t>(TokenKind::Star)]      = {PREC_MULT, RegOp::MUL, false};
  tab[static_cast<uint16_t>(TokenKind::SlashChar)] = {PREC_MULT, RegOp::DIV, false};
  tab[static_cast<uint16_t>(TokenKind::Percent)]   = {PREC_MULT, RegOp::MOD, false};
  tab[static_cast<uint16_t>(TokenKind::Plus)]      = {PREC_ADD, RegOp::ADD, false};
  tab[static_cast<uint16_t>(TokenKind::Minus)]     = {PREC_ADD, RegOp::SUB, false};
  tab[static_cast<uint16_t>(TokenKind::Shl)]       = {PREC_SHIFT, RegOp::SHL, false};
  tab[static_cast<uint16_t>(TokenKind::Sar)]       = {PREC_SHIFT, RegOp::SAR, false};
  tab[static_cast<uint16_t>(TokenKind::Shr)]       = {PREC_SHIFT, RegOp::SHR, false};
  tab[static_cast<uint16_t>(TokenKind::Less)]      = {PREC_COMPARE, RegOp::LT, false};
  tab[static_cast<uint16_t>(TokenKind::Greater)]   = {PREC_COMPARE, RegOp::GT, false};
  tab[static_cast<uint16_t>(TokenKind::Lte)]       = {PREC_COMPARE, RegOp::LTE, false};
  tab[static_cast<uint16_t>(TokenKind::Gte)]       = {PREC_COMPARE, RegOp::GTE, false};
  tab[static_cast<uint16_t>(TokenKind::Eq)]        = {PREC_EQ, RegOp::EQ, false};
  tab[static_cast<uint16_t>(TokenKind::StrictEq)]  = {PREC_EQ, RegOp::SEQ, false};
  tab[static_cast<uint16_t>(TokenKind::Neq)]       = {PREC_EQ, RegOp::NEQ, false};
  tab[static_cast<uint16_t>(TokenKind::StrictNeq)] = {PREC_EQ, RegOp::SNEQ, false};
  tab[static_cast<uint16_t>(TokenKind::Amp)]       = {PREC_BIT_AND, RegOp::AND, false};
  tab[static_cast<uint16_t>(TokenKind::Caret)]     = {PREC_BIT_XOR, RegOp::XOR, false};
  tab[static_cast<uint16_t>(TokenKind::Pipe)]      = {PREC_BIT_OR, RegOp::OR, false};
  tab[static_cast<uint16_t>(TokenKind::Pow)]       = {PREC_POW, RegOp::POW, true};
  return tab;
}();

constexpr inline auto kLogicalPrec = []() constexpr {
  std::array<uint8_t, kTokenTableSize> tab{};
  tab[static_cast<uint16_t>(TokenKind::Land)]               = PREC_LOGICAL_AND;
  tab[static_cast<uint16_t>(TokenKind::Lor)]                = PREC_LOGICAL_OR;
  tab[static_cast<uint16_t>(TokenKind::DoubleQuestionMark)] = PREC_LOGICAL_OR;
  return tab;
}();

constexpr auto kCompoundTable = []() constexpr {
  std::array<RegOp, kTokenTableSize> tab{};
  tab[static_cast<uint16_t>(TokenKind::MulAssign)]   = RegOp::MUL;
  tab[static_cast<uint16_t>(TokenKind::DivAssign)]   = RegOp::DIV;
  tab[static_cast<uint16_t>(TokenKind::ModAssign)]   = RegOp::MOD;
  tab[static_cast<uint16_t>(TokenKind::PlusAssign)]  = RegOp::ADD;
  tab[static_cast<uint16_t>(TokenKind::MinusAssign)] = RegOp::SUB;
  tab[static_cast<uint16_t>(TokenKind::PowAssign)]   = RegOp::POW;
  tab[static_cast<uint16_t>(TokenKind::ShlAssign)]   = RegOp::SHL;
  tab[static_cast<uint16_t>(TokenKind::SarAssign)]   = RegOp::SAR;
  tab[static_cast<uint16_t>(TokenKind::ShrAssign)]   = RegOp::SHR;
  tab[static_cast<uint16_t>(TokenKind::AndAssign)]   = RegOp::AND;
  tab[static_cast<uint16_t>(TokenKind::XorAssign)]   = RegOp::XOR;
  tab[static_cast<uint16_t>(TokenKind::OrAssign)]    = RegOp::OR;
  return tab;
}();

// ─── Pratt infix table — precedence + associativity ────────────────────────

struct InfixEntry {
  uint8_t prec;
  bool right_assoc;
};

constexpr auto kInfixTable = []() constexpr {
  std::array<InfixEntry, kTokenTableSize> tab{};
  // Binary operators
  tab[static_cast<uint16_t>(TokenKind::Star)]      = {PREC_MULT, false};
  tab[static_cast<uint16_t>(TokenKind::SlashChar)] = {PREC_MULT, false};
  tab[static_cast<uint16_t>(TokenKind::Percent)]   = {PREC_MULT, false};
  tab[static_cast<uint16_t>(TokenKind::Plus)]      = {PREC_ADD, false};
  tab[static_cast<uint16_t>(TokenKind::Minus)]     = {PREC_ADD, false};
  tab[static_cast<uint16_t>(TokenKind::Shl)]       = {PREC_SHIFT, false};
  tab[static_cast<uint16_t>(TokenKind::Sar)]       = {PREC_SHIFT, false};
  tab[static_cast<uint16_t>(TokenKind::Shr)]       = {PREC_SHIFT, false};
  tab[static_cast<uint16_t>(TokenKind::Less)]      = {PREC_COMPARE, false};
  tab[static_cast<uint16_t>(TokenKind::Greater)]   = {PREC_COMPARE, false};
  tab[static_cast<uint16_t>(TokenKind::Lte)]       = {PREC_COMPARE, false};
  tab[static_cast<uint16_t>(TokenKind::Gte)]       = {PREC_COMPARE, false};
  tab[static_cast<uint16_t>(TokenKind::Eq)]        = {PREC_EQ, false};
  tab[static_cast<uint16_t>(TokenKind::StrictEq)]  = {PREC_EQ, false};
  tab[static_cast<uint16_t>(TokenKind::Neq)]       = {PREC_EQ, false};
  tab[static_cast<uint16_t>(TokenKind::StrictNeq)] = {PREC_EQ, false};
  tab[static_cast<uint16_t>(TokenKind::Amp)]       = {PREC_BIT_AND, false};
  tab[static_cast<uint16_t>(TokenKind::Caret)]     = {PREC_BIT_XOR, false};
  tab[static_cast<uint16_t>(TokenKind::Pipe)]      = {PREC_BIT_OR, false};
  tab[static_cast<uint16_t>(TokenKind::Pow)]       = {PREC_POW, true};
  // Logical operators
  tab[static_cast<uint16_t>(TokenKind::Land)]               = {PREC_LOGICAL_AND, false};
  tab[static_cast<uint16_t>(TokenKind::Lor)]                = {PREC_LOGICAL_OR, false};
  tab[static_cast<uint16_t>(TokenKind::DoubleQuestionMark)] = {PREC_LOGICAL_OR, false};
  // Postfix / call / member
  tab[static_cast<uint16_t>(TokenKind::Inc)]      = {PREC_POSTFIX, false};
  tab[static_cast<uint16_t>(TokenKind::Dec)]      = {PREC_POSTFIX, false};
  tab[static_cast<uint16_t>(TokenKind::LParen)]   = {PREC_CALL, false};
  tab[static_cast<uint16_t>(TokenKind::Dot)]      = {PREC_CALL, false};
  tab[static_cast<uint16_t>(TokenKind::LBracket)] = {PREC_CALL, false};
  // Ternary
  tab[static_cast<uint16_t>(TokenKind::Question)] = {PREC_COND, true};
  return tab;
}();

// ─── Operator table accessors (preserved for bytecode emission) ─────────────

inline constexpr RegOp binop_to_reg(TokenKind tok) { return kBinOpTable[static_cast<uint16_t>(tok)].opcode; }

inline constexpr RegOp compound_to_binop(TokenKind tok) { return kCompoundTable[static_cast<uint16_t>(tok)]; }

inline constexpr bool is_right_assoc(TokenKind tok) { return kInfixTable[static_cast<uint16_t>(tok)].right_assoc; }

} // namespace qjsp
