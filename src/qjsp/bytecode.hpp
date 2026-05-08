#pragma once

#include "atom.hpp"
#include "gc.hpp"
#include "value.hpp"
#include <cstdint>

namespace qjsp {

struct Context;

enum class VarDefKind : uint8_t {
  unknown_,
  var_,
  let,
  const_,
  function_decl,
  catch_,
};

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

enum class ClosureType : uint8_t { local, arg, ref, global_ref, global_decl, global, module_decl, module_import };

struct ClosureVar {
  // flags bit layout:
  //   [2:0]  ClosureType  (3 bits, values 0-7)
  //   [3]    is_lexical   (1 bit)
  //   [4]    is_const     (1 bit)
  //   [7:5]  VarDefKind   (3 bits, values 0-5)
  uint8_t flags;
  uint16_t var_idx;
  Atom var_name;

  ClosureType closure_type() const { return static_cast<ClosureType>(flags & 0x07); }

  void set_closure_type(ClosureType t) { flags = (flags & ~0x07u) | static_cast<uint8_t>(t); }

  bool is_lexical() const { return (flags >> 3) & 1; }
  void set_is_lexical(bool v) {
    if (v)
      flags |= 0x08;
    else
      flags &= ~0x08u;
  }

  bool is_const() const { return (flags >> 4) & 1; }
  void set_is_const(bool v) {
    if (v)
      flags |= 0x10;
    else
      flags &= ~0x10u;
  }

  VarDefKind var_kind() const { return static_cast<VarDefKind>(flags >> 5); }
  void set_var_kind(VarDefKind k) { flags = (flags & 0x1Fu) | static_cast<uint8_t>(static_cast<uint8_t>(k) << 5); }
};

constexpr int kArgScopeIndex = 1;
constexpr int kArgScopeEnd   = -2;

struct BytecodeVarDef {
  // flags bit layout:
  //   [0]    is_const     (1 bit)
  //   [1]    is_lexical   (1 bit)
  //   [2]    is_captured  (1 bit)
  //   [3]    has_scope    (1 bit)
  //   [4]    unused       (1 bit)
  //   [7:5]  VarDefKind   (3 bits, values 0-5)
  Atom var_name;
  int scope_next;
  uint8_t flags;
  uint16_t var_ref_idx;

  bool is_const() const { return flags & 0x01; }
  void set_is_const(bool v) {
    if (v)
      flags |= 0x01;
    else
      flags &= ~0x01u;
  }

  bool is_lexical() const { return (flags >> 1) & 1; }
  void set_is_lexical(bool v) {
    if (v)
      flags |= 0x02;
    else
      flags &= ~0x02u;
  }

  bool is_captured() const { return (flags >> 2) & 1; }
  void set_is_captured(bool v) {
    if (v)
      flags |= 0x04;
    else
      flags &= ~0x04u;
  }

  bool has_scope() const { return (flags >> 3) & 1; }
  void set_has_scope(bool v) {
    if (v)
      flags |= 0x08;
    else
      flags &= ~0x08u;
  }

  VarDefKind var_kind() const { return static_cast<VarDefKind>(flags >> 5); }
  void set_var_kind(VarDefKind k) { flags = (flags & 0x1Fu) | static_cast<uint8_t>(static_cast<uint8_t>(k) << 5); }
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

  uint8_t *byte_code_buf;
  BytecodeVarDef *vardefs;
  ClosureVar *closure_var;
  Value *cpool;
  Context *realm;

  int cpool_count;
  int closure_var_count;
  int byte_code_len;
  int instr_count; // number of 32-bit instructions (reg VM)
  Atom func_name;

  uint16_t reg_count; // total registers per frame (reg VM)
  uint16_t arg_count;
  uint16_t var_count;
  uint16_t defined_arg_count;
  uint16_t stack_size;
  uint16_t var_ref_count;

  uint8_t js_mode;
  // flags1 bit layout:
  //   [0]    has_prototype                (1 bit)
  //   [1]    has_simple_parameter_list    (1 bit)
  //   [2]    is_derived_class_constructor (1 bit)
  //   [3]    need_home_object             (1 bit)
  //   [5:4]  func_kind                    (2 bits, FunctionKind)
  //   [6]    new_target_allowed           (1 bit)
  //   [7]    super_call_allowed           (1 bit)
  uint8_t flags1;
  // flags2 bit layout:
  //   [0]  super_allowed              (1 bit)
  //   [1]  arguments_allowed          (1 bit)
  //   [2]  has_debug                  (1 bit)
  //   [3]  read_only_bytecode         (1 bit)
  //   [4]  is_direct_or_indirect_eval (1 bit)
  //   [7:5] unused                    (3 bits)
  uint8_t flags2;

  bool has_prototype() const { return flags1 & 0x01; }
  bool has_simple_parameter_list() const { return (flags1 >> 1) & 1; }
  bool is_derived_class_constructor() const { return (flags1 >> 2) & 1; }
  bool need_home_object() const { return (flags1 >> 3) & 1; }
  FunctionKind func_kind() const { return static_cast<FunctionKind>((flags1 >> 4) & 0x03); }
  bool new_target_allowed() const { return (flags1 >> 6) & 1; }
  bool super_call_allowed() const { return (flags1 >> 7) & 1; }

  bool super_allowed() const { return flags2 & 0x01; }
  bool arguments_allowed() const { return (flags2 >> 1) & 1; }
  bool has_debug() const { return (flags2 >> 2) & 1; }
  bool read_only_bytecode() const { return (flags2 >> 3) & 1; }
  bool is_direct_or_indirect_eval() const { return (flags2 >> 4) & 1; }

  struct {
    Atom filename;
    int source_len;
    int pc2line_len;
    uint8_t *pc2line_buf;
    char *source;
  } debug;
};

} // namespace qjsp
