#include "qjsp/context.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace qjsp {
// ─── Lowering ───────────────────────────────────────────────────────────────

FunctionBytecode *lower_reg(FunctionDef *fd, Context *ctx) {
  auto *b        = new FunctionBytecode();
  b->ref_count   = 1;
  b->gc_obj_type = GCObjType::function_bytecode;
  b->realm       = ctx;

  ctx->rt->add_gc_object(b);

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
    RegOp op = static_cast<RegOp>(instr.opcode());
    if (op == RegOp::CATCH) {
      // CATCH uses absolute target in iABx
      fd->instructions[static_cast<size_t>(p.instr_idx)] =
          Instruction::iABx(static_cast<uint8_t>(op), instr.a(),
                            static_cast<uint16_t>(target)).raw;
    } else {
      // JMP, IS_FALSE, IS_TRUE: relative offset from _next_ instruction
      int rel_off = offset - 1;
      fd->instructions[static_cast<size_t>(p.instr_idx)] =
          Instruction::iAsBx(static_cast<uint8_t>(op), instr.a(),
                             static_cast<int16_t>(rel_off)).raw;
    }
  }

  // ── Copy instructions ─────────────────────────────────────────────────────
  b->instr_count   = static_cast<uint32_t>(fd->instructions.size());
  b->byte_code_buf = std::make_unique<uint8_t[]>(static_cast<size_t>(b->instr_count) * 4);
  std::memcpy(b->byte_code_buf.get(), fd->instructions.data(), static_cast<size_t>(b->instr_count) * 4);
  b->byte_code_len = b->instr_count * 4;

  // Copy constant pool
  b->cpool_count = static_cast<uint32_t>(fd->cpool.size());
  b->cpool       = std::make_unique<Value[]>(static_cast<size_t>(b->cpool_count));
  for (uint32_t i = 0; i < b->cpool_count; i++)
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
  b->closure_var_count = static_cast<uint32_t>(fd->closure_var.size());
  if (b->closure_var_count > 0) {
    b->closure_var = std::make_unique<ClosureVar[]>(static_cast<size_t>(b->closure_var_count));
    for (uint32_t i = 0; i < b->closure_var_count; i++)
      b->closure_var[i] = fd->closure_var[static_cast<size_t>(i)];
  }

  // Var defs (merged args + vars)
  int total_defs = fd->arg_count + fd->var_count;
  b->var_count   = static_cast<uint16_t>(total_defs);
  if (total_defs > 0) {
    b->vardefs = std::make_unique<BytecodeVarDef[]>(static_cast<size_t>(total_defs));
    for (int i = 0; i < fd->arg_count; i++) {
      auto &vd                  = fd->args[static_cast<size_t>(i)];
      b->vardefs[i].var_name    = vd.var_name;
      b->vardefs[i].scope_next  = vd.scope_next;
      b->vardefs[i].flags       = 0;
      b->vardefs[i].set_is_captured(vd.is_captured);
      b->vardefs[i].var_ref_idx = static_cast<uint16_t>(vd.upval_idx >= 0 ? vd.upval_idx : 0);
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
      b->vardefs[base + i].var_ref_idx = static_cast<uint16_t>(vd.upval_idx >= 0 ? vd.upval_idx : 0);
    }
  }

  return b;
}

} // namespace qjsp
