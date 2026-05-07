#pragma once

#include "atom.hpp"
#include "gc.hpp"
#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct Context;

enum class ClosureType : uint8_t { local, arg, ref, global_ref, global_decl, global, module_decl, module_import };

struct ClosureVar {
  uint8_t flags;
  uint16_t var_idx;
  Atom var_name;

  ClosureType closure_type() const { return static_cast<ClosureType>(flags & 0x07); }
  bool is_lexical() const { return (flags >> 3) & 1; }
  bool is_const() const { return (flags >> 4) & 1; }
  uint8_t var_kind() const { return flags >> 5; }
  void set_closure_type(ClosureType t) { flags = (flags & ~0x07u) | static_cast<uint8_t>(t); }
  void set_is_lexical(bool v) {
    if (v)
      flags |= 0x08;
    else
      flags &= ~0x08u;
  }
  void set_is_const(bool v) {
    if (v)
      flags |= 0x10;
    else
      flags &= ~0x10u;
  }
  void set_var_kind(uint8_t k) { flags = (flags & 0x1Fu) | static_cast<uint8_t>(k << 5); }
};

constexpr int kArgScopeIndex = 1;
constexpr int kArgScopeEnd   = -2;

enum class VarKind : uint8_t {
  normal,
  function_decl,
  new_function_decl,
  catch_,
  function_name,
  private_field,
  private_method,
  private_getter,
  private_setter,
  private_getter_setter,
  global_function_decl,
};

struct BytecodeVarDef {
  Atom var_name;
  int scope_next;
  uint8_t flags;
  uint16_t var_ref_idx;

  bool is_const() const { return flags & 0x01; }
  bool is_lexical() const { return (flags >> 1) & 1; }
  bool is_captured() const { return (flags >> 2) & 1; }
  bool has_scope() const { return (flags >> 3) & 1; }
  uint8_t var_kind() const { return flags >> 4; }
  void set_is_const(bool v) {
    if (v)
      flags |= 0x01;
    else
      flags &= ~0x01u;
  }
  void set_is_lexical(bool v) {
    if (v)
      flags |= 0x02;
    else
      flags &= ~0x02u;
  }
  void set_is_captured(bool v) {
    if (v)
      flags |= 0x04;
    else
      flags &= ~0x04u;
  }
  void set_has_scope(bool v) {
    if (v)
      flags |= 0x08;
    else
      flags &= ~0x08u;
  }
  void set_var_kind(uint8_t k) { flags = (flags & 0x0Fu) | static_cast<uint8_t>(k << 4); }
};

constexpr int kPC2LineBase      = -1;
constexpr int kPC2LineRange     = 5;
constexpr int kPC2LineOpFirst   = 1;
constexpr int kPC2LineDiffPCMax = (255 - kPC2LineOpFirst) / kPC2LineRange;

enum class FunctionKind : uint8_t {
  normal          = 0,
  generator       = (1 << 0),
  async           = (1 << 1),
  async_generator = (1 << 0) | (1 << 1),
};

struct FunctionBytecode : GCObjectHeader {
  uint8_t js_mode;
  uint8_t flags1;
  uint8_t flags2;

  bool has_prototype() const { return flags1 & 0x01; }
  bool has_simple_parameter_list() const { return (flags1 >> 1) & 1; }
  bool is_derived_class_constructor() const { return (flags1 >> 2) & 1; }
  bool need_home_object() const { return (flags1 >> 3) & 1; }
  uint8_t func_kind() const { return (flags1 >> 4) & 0x03; }
  bool new_target_allowed() const { return (flags1 >> 6) & 1; }
  bool super_call_allowed() const { return (flags1 >> 7) & 1; }

  bool super_allowed() const { return flags2 & 0x01; }
  bool arguments_allowed() const { return (flags2 >> 1) & 1; }
  bool has_debug() const { return (flags2 >> 2) & 1; }
  bool read_only_bytecode() const { return (flags2 >> 3) & 1; }
  bool is_direct_or_indirect_eval() const { return (flags2 >> 4) & 1; }

  uint8_t *byte_code_buf;
  int byte_code_len;
  int instr_count;    // number of 32-bit instructions (reg VM)
  uint16_t reg_count; // total registers per frame (reg VM)
  Atom func_name;
  BytecodeVarDef *vardefs;
  ClosureVar *closure_var;
  uint16_t arg_count;
  uint16_t var_count;
  uint16_t defined_arg_count;
  uint16_t stack_size;
  uint16_t var_ref_count;
  Context *realm;
  Value *cpool;
  int cpool_count;
  int closure_var_count;
  struct {
    Atom filename;
    int source_len;
    int pc2line_len;
    uint8_t *pc2line_buf;
    char *source;
  } debug;
};

} // namespace qjsp
