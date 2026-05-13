#include "qjsp/engine.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace qjsp {

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

int FunctionDef::emit_jump(RegOp op, int label, uint8_t reg_a) {
  if (label < 0)
    return -1;
  label_slots[static_cast<size_t>(label)].ref_count++;
  if (op == RegOp::CATCH) {
    emit_iABx(op, reg_a, 0);
  } else {
    emit_iAsBx(op, reg_a, 0);
  }
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
    if (vars[static_cast<size_t>(i)].var_name == name)
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
  auto idx = var_count++;
  vars.emplace_back();
  vars[idx].var_name  = name;
  vars[idx].reg_index = 1 + arg_count + idx;
  alloc.var_count_    = var_count;
  auto min_temp       = 1 + alloc.arg_count_ + alloc.var_count_;
  if (alloc.next_temp_ < min_temp)
    alloc.next_temp_ = min_temp;
  if (alloc.max_temp_ < min_temp)
    alloc.max_temp_ = min_temp;
  return idx;
}

int FunctionDef::add_arg(Atom name) {
  auto idx = arg_count++;
  args.emplace_back();
  args[static_cast<size_t>(idx)].var_name  = name;
  args[static_cast<size_t>(idx)].reg_index = 1 + idx;
  alloc.arg_count_                         = arg_count;
  auto min_temp                            = 1 + alloc.arg_count_ + alloc.var_count_;
  if (alloc.next_temp_ < min_temp)
    alloc.next_temp_ = min_temp;
  if (alloc.max_temp_ < min_temp)
    alloc.max_temp_ = min_temp;
  return idx;
}

int FunctionDef::first_lexical_var(int scope) {
  while (scope >= 0) {
    auto idx = scopes[static_cast<size_t>(scope)].first;
    if (idx >= 0)
      return idx;
    scope = scopes[static_cast<size_t>(scope)].parent;
  }
  return -1;
}

int FunctionDef::push_scope() {
  int scope = static_cast<int>(scopes.size());
  scopes.emplace_back();
  scopes[static_cast<size_t>(scope)].parent = scope_level;
  scopes[static_cast<size_t>(scope)].first  = scope_first;
  emit_iABx(RegOp::NOP, 0, 0);
  scope_level = scope;
  return scope;
}

void FunctionDef::pop_scope() {
  int scope   = scope_level;
  scope_level = scopes[static_cast<size_t>(scope)].parent;
  scope_first = first_lexical_var(scope_level);
  emit_iABx(RegOp::NOP, 0, 0);
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
  lexer.init(e_, filename, reinterpret_cast<const uint8_t *>(source), std::strlen(source));
}

bool RegParseState::expect(TokenKind tok) {
  if (lexer.token.kind != tok)
    return false;
  return next_token();
}

bool RegParseState::js_define_var(Atom name, TokenKind tok) {
  FunctionDef *fd = cur_func;
  switch (tok) {
  case TokenKind::KwLet:
    if (fd->find_scope_var(name, fd->scope_level))
      return false;
    {
      int idx = fd->add_var(name);
      if (idx < 0)
        return false;
      VarDef &vd                                             = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind                                            = VarDefKind::let;
      vd.scope_level                                         = fd->scope_level;
      vd.scope_next                                          = fd->scope_first;
      vd.is_lexical                                          = true;
      fd->scopes[static_cast<size_t>(fd->scope_level)].first = idx;
      fd->scope_first                                        = idx;
    }
    return true;
  case TokenKind::KwConst:
    if (fd->find_scope_var(name, fd->scope_level))
      return false;
    {
      int idx = fd->add_var(name);
      if (idx < 0)
        return false;
      VarDef &vd                                             = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind                                            = VarDefKind::const_;
      vd.scope_level                                         = fd->scope_level;
      vd.scope_next                                          = fd->scope_first;
      vd.is_lexical                                          = true;
      vd.is_const                                            = true;
      fd->scopes[static_cast<size_t>(fd->scope_level)].first = idx;
      fd->scope_first                                        = idx;
    }
    return true;
  case TokenKind::KwVar:
    if (fd->find_var(name) < 0) {
      int idx = fd->add_var(name);
      if (idx < 0)
        return false;
      VarDef &vd     = fd->vars[static_cast<size_t>(idx)];
      vd.var_kind    = VarDefKind::var_;
      vd.scope_level = 0;
      vd.scope_next  = -1;
    }
    return true;
  default:
    return false;
  }
}

} // namespace qjsp

// ─── FunctionDef closure helpers ─────────────────────────────────────────────

namespace qjsp {

int FunctionDef::capture_var(VarDef *vd) {
  if (!vd->is_captured) {
    vd->is_captured = true;
    vd->upval_idx   = next_upval++;
    var_ref_count   = next_upval;
  }
  return vd->upval_idx;
}

bool FunctionDef::find_enclosing_var(Atom name, VarDef *&vd, FunctionDef *&owner, int &var_idx, bool &is_arg) {
  FunctionDef *fd = this;
  while (fd) {
    for (int i = 0; i < fd->var_count; i++) {
      if (fd->vars[static_cast<size_t>(i)].var_name == name && fd->vars[static_cast<size_t>(i)].scope_level == 0) {
        vd      = &fd->vars[static_cast<size_t>(i)];
        owner   = fd;
        var_idx = i;
        is_arg  = false;
        return true;
      }
    }
    for (int i = 0; i < fd->arg_count; i++) {
      if (fd->args[static_cast<size_t>(i)].var_name == name) {
        vd      = &fd->args[static_cast<size_t>(i)];
        owner   = fd;
        var_idx = i;
        is_arg  = true;
        return true;
      }
    }
    fd = fd->parent;
  }
  vd    = nullptr;
  owner = nullptr;
  return false;
}

int FunctionDef::resolve_upval(Atom name) {
  VarDef *vd         = nullptr;
  FunctionDef *owner = nullptr;
  int var_idx        = 0;
  bool is_arg        = false;

  if (!find_enclosing_var(name, vd, owner, var_idx, is_arg))
    return -1;
  if (owner == this)
    return -1;

  owner->capture_var(vd);

  for (size_t i = 0; i < closure_var.size(); i++) {
    if (closure_var[i].var_name == name)
      return static_cast<int>(i);
  }

  ClosureVar cv;
  cv.var_name = name;
  cv.var_idx  = static_cast<uint16_t>(var_idx);
  cv.set_closure_type(is_arg ? ClosureType::arg : ClosureType::local);
  cv.set_is_const(vd->is_const);
  cv.set_is_lexical(vd->is_lexical);
  cv.set_var_kind(vd->var_kind);
  closure_var.push_back(cv);
  return static_cast<int>(closure_var.size()) - 1;
}
} // namespace qjsp
