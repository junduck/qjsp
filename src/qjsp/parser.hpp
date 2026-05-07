#pragma once

#include "qjsp/atom.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/lexer.hpp"
#include "qjsp/value.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qjsp {

struct Runtime;
struct Context;

// ─── Opcodes ────────────────────────────────────────────────────────────────

// Mirrors quickjs-opcode.h. The full set; only a subset is emitted in the
// initial parser pass.
enum OpCode : uint8_t {
#define DEF(id, size, n_pop, n_push, f) OP_##id,
#include "qjsp/quickjs-opcode.h"
#undef DEF
};

// Enum values for define_method / define_method_computed u8 operand.
constexpr uint8_t OP_DEFINE_METHOD_METHOD   = 0;
constexpr uint8_t OP_DEFINE_METHOD_GETTER   = (1 << 0);
constexpr uint8_t OP_DEFINE_METHOD_ENUMERABLE = (1 << 1);

// Enum values for throw_error u8 operand.
constexpr uint8_t JS_THROW_ERROR_ITERATOR_THROW = 5;

// Consts used in the variable system.
constexpr int JS_MAX_LOCAL_VARS = 65535;
constexpr int ARGUMENT_VAR_OFFSET = (1 << 15);
constexpr int GLOBAL_VAR_OFFSET = 0x40000000;

// Opcode format enum (must match OP_FMT_xxx in quickjs-opcode.h).
enum OpCodeFmt : uint8_t {
#define FMT(f) OP_FMT_##f,
#include "qjsp/quickjs-opcode.h"
#undef FMT
};

struct OpCodeInfo {
  uint8_t size;
  uint8_t n_pop;
  uint8_t n_push;
  uint8_t fmt;
};

extern const OpCodeInfo kOpInfo[];

// ─── LabelSlot ──────────────────────────────────────────────────────────────

struct LabelSlot {
  int ref_count = 0;
  int pos = -1;    // phase-1 address, -1 = unresolved
  int pos2 = -1;   // phase-2 address
  int addr = -1;   // phase-3 address
};

// ─── VarScope ───────────────────────────────────────────────────────────────

struct VarScope {
  int parent = -1;  // index into fd->scopes of the enclosing scope
  int first = -1;   // index into fd->vars of the last variable in this scope
};

// ─── VarDef ─────────────────────────────────────────────────────────────────

enum class VarDefKind : uint8_t {
  with_,
  let,
  const_,
  function_decl,
  new_function_decl,
  catch_,
  var_,
};

struct VarDef {
  Atom var_name = kAtomNull;
  int scope_level = 0;
  int scope_next = 0;
  bool is_const = false;
  bool is_lexical = false;
  bool is_captured = false;
  uint8_t var_kind = 0;
  int func_pool_idx = -1;
};

// ─── BlockEnv (break/continue stack) ────────────────────────────────────────

struct BlockEnv {
  BlockEnv *prev = nullptr;
  Atom label_name = kAtomNull;
  int label_break = -1;
  int label_cont = -1;
  int drop_count = 0;
  int label_finally = -1;
  int scope_level = 0;
  bool has_iterator = false;
  bool is_regular_stmt = false;
};

// ─── FunctionDef ────────────────────────────────────────────────────────────

/// Internal compilation unit. Not GC-tracked during compilation.
/// Lowered to FunctionBytecode (gc.hpp) when compilation finishes.
struct FunctionDef {
  Runtime *rt = nullptr;
  FunctionDef *parent = nullptr;
  int parent_cpool_idx = -1;
  int parent_scope_level = 0;

  bool is_eval = false;
  bool is_global_var = false;
  bool is_func_expr = false;
  bool has_home_object = false;
  bool has_prototype = false;
  bool has_simple_parameter_list = true;
  bool has_parameter_expressions = false;
  bool has_use_strict = false;
  bool has_eval_call = false;
  bool has_arguments_binding = false;
  bool has_this_binding = false;
  bool new_target_allowed = false;
  bool super_call_allowed = false;
  bool super_allowed = false;
  bool arguments_allowed = false;
  bool is_derived_class_constructor = false;
  bool in_function_body = false;
  bool has_await = false;
  uint8_t js_mode = 0; // bitmask: JS_MODE_STRICT etc.

  FunctionKind func_kind = FunctionKind::normal;
  Atom func_name = kAtomNull;

  // Variable tables.
  std::vector<VarDef> vars;
  std::vector<VarDef> args;
  int var_count = 0;
  int arg_count = 0;
  int defined_arg_count = 0;
  int var_ref_count = 0;

  // Special variable indices (-1 = none).
  int var_object_idx = -1;
  int arg_var_object_idx = -1;
  int arguments_var_idx = -1;
  int arguments_arg_idx = -1;
  int func_var_idx = -1;
  int eval_ret_idx = -1;
  int this_var_idx = -1;
  int new_target_var_idx = -1;
  int this_active_func_var_idx = -1;
  int home_object_var_idx = -1;
  bool need_home_object = false;

  // Scopes.
  int scope_level = -1;
  int scope_first = -1;
  std::vector<VarScope> scopes;
  int body_scope = 0;

  // Bytecode buffer.
  std::vector<uint8_t> byte_code;
  int last_opcode_pos = -1;
  const uint8_t *last_opcode_source_ptr = nullptr;
  bool use_short_opcodes = false;
  bool byte_code_has_error = false;

  // Labels (for forward-jump resolution).
  std::vector<LabelSlot> label_slots;
  int label_count = 0;

  // Constant pool.
  std::vector<Value> cpool;

  // Break/continue stack (intrusive linked list via BlockEnv::prev).
  BlockEnv *top_break = nullptr;

  // Child FunctionDefs (for later lowering)
  std::vector<FunctionDef *> children;

  // Source info.
  Atom filename = kAtomNull;
  std::string source;  // raw source, UTF-8

  FunctionDef(Runtime *rt_) : rt(rt_) {}

  // ── emitter helpers ───────────────────────────────────────────────────

  void emit_u8(uint8_t v);
  void emit_u16(uint16_t v);
  void emit_u32(uint32_t v);
  void emit_atom(Atom a);
  void emit_op(uint8_t op);
  int new_label();
  int emit_label(int label);
  int emit_goto(uint8_t opcode, int label);
  int cpool_add(Value val);
  uint8_t prev_opcode() const;
  bool is_live_code() const;

  // ── variable helpers ──────────────────────────────────────────────────

  int find_var(Atom name);
  int find_arg(Atom name);
  int add_var(Atom name);
  int add_arg(Atom name);
  VarDef *find_scope_var(Atom name, int scope);

  // ── scope helpers ─────────────────────────────────────────────────────

  int push_scope();
  void pop_scope();
  void close_scopes(int scope, int scope_stop);

  // ── break/continue helpers ────────────────────────────────────────────

  void push_break(BlockEnv *be, Atom label, int lbreak, int lcont, int drops);
  void pop_break();
};

// ─── ParseState ─────────────────────────────────────────────────────────────

enum ParseFuncType : uint8_t {
  PARSE_FUNC_STATEMENT,
  PARSE_FUNC_VAR,
  PARSE_FUNC_EXPR,
  PARSE_FUNC_ARROW,
  PARSE_FUNC_GETTER,
  PARSE_FUNC_SETTER,
  PARSE_FUNC_METHOD,
  PARSE_FUNC_CLASS_STATIC_INIT,
  PARSE_FUNC_CLASS_CONSTRUCTOR,
  PARSE_FUNC_DERIVED_CLASS_CONSTRUCTOR,
};

// Allocation modes for variable declarations.
enum VarDeclMode : uint8_t {
  DECL_MASK_OTHER = (1 << 0),
  DECL_MASK_FUNC  = (1 << 1),
  DECL_MASK_FUNC_WITH_LABEL = (1 << 2),
  DECL_MASK_ALL = 0xFF,
};

struct ParseState {
  Runtime *rt;
  Context *ctx;
  Lexer lexer;
  FunctionDef *cur_func = nullptr;
  bool is_module = false;

  ParseState(Runtime *rt_, Context *ctx_) : rt(rt_), ctx(ctx_) {}

  // ── initialisation ────────────────────────────────────────────────────

  void init(const char *source, const char *filename);

  // ── compilation entry ─────────────────────────────────────────────────

  bool compile();

  // ── token helpers ─────────────────────────────────────────────────────

  bool next_token() { return lexer.next_token(); }
  int peek_token(bool no_lf) { return lexer.peek_token(no_lf); }
  bool expect(int tok);

  // ── convenience emitter shortcuts ─────────────────────────────────────

  void emit_u8(uint8_t v) { cur_func->emit_u8(v); }
  void emit_u16(uint16_t v) { cur_func->emit_u16(v); }
  void emit_u32(uint32_t v) { cur_func->emit_u32(v); }
  void emit_op(uint8_t op) { cur_func->emit_op(op); }
  void emit_atom(Atom a) { cur_func->emit_atom(a); }
  int emit_label(int l) { return cur_func->emit_label(l); }
  int new_label() { return cur_func->new_label(); }
  int emit_goto(uint8_t op, int l) { return cur_func->emit_goto(op, l); }
  int cpool_add(Value v) { return cur_func->cpool_add(v); }

private:
  // ── parser internals ──────────────────────────────────────────────────

  // expressions
  bool parse_expr();
  bool parse_expr2(int flags);
  bool parse_assign_expr();
  bool parse_assign_expr2(int flags);
  bool parse_cond_expr(int flags);
  bool parse_postfix_expr(int flags);
  bool parse_unary(int flags);
  bool parse_primary_expr();
  bool parse_object_literal();
  bool parse_array_literal();

  // arrow function helper
  bool parse_arrow_body(Atom single_arg);

  // statements
  bool parse_statement();
  bool parse_statement_or_decl(int decl_mask);
  bool parse_block();
  bool parse_if_statement();
  bool parse_var_decls(int tok, int flags);
  bool parse_return_statement();
  bool parse_while_statement();
  bool parse_for_statement();
  bool parse_do_statement();
  bool parse_switch_statement();
  bool parse_try_statement();
  bool parse_expr_statement();
  bool parse_throw_statement();

  // function declaration
  struct FunctionDef *parse_function_decl(ParseFuncType func_type,
                                          FunctionKind func_kind,
                                          Atom func_name,
                                          const uint8_t *ptr,
                                          bool is_export);

  // helpers
  bool emit_push_const(Value val, bool as_atom);
  void emit_return(bool has_val);
  bool js_define_var(Atom name, int tok);
  bool is_label();
  int js_is_let(int decl_mask);
  void push_enter_scope();
  void pop_leave_scope();
  void set_eval_ret_undefined();

  FunctionDef *new_function_def(bool is_eval, bool is_func_expr,
                                const uint8_t *source_ptr);
};

// ─── Expression parse flags ─────────────────────────────────────────────────

constexpr int PF_IN_ACCEPTED   = (1 << 0);
constexpr int PF_POSTFIX_CALL  = (1 << 1);
constexpr int PF_POW_ALLOWED   = (1 << 2);
constexpr int PF_POW_FORBIDDEN = (1 << 3);

// ─── JS_MODE flags ──────────────────────────────────────────────────────────

constexpr uint8_t JS_MODE_STRICT = (1 << 0);
constexpr uint8_t JS_MODE_ASYNC  = (1 << 2);

} // namespace qjsp
