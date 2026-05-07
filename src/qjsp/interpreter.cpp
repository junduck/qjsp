#include "qjsp/interpreter.hpp"
#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace qjsp {

// ─── Helpers ────────────────────────────────────────────────────────────────

Runtime *Interpreter::rt() const { return ctx_->rt; }

uint32_t read_u32(const uint8_t *&pc) {
  uint32_t v = static_cast<uint32_t>(pc[0]) |
               (static_cast<uint32_t>(pc[1]) << 8) |
               (static_cast<uint32_t>(pc[2]) << 16) |
               (static_cast<uint32_t>(pc[3]) << 24);
  pc += 4;
  return v;
}

int32_t read_i32(const uint8_t *&pc) {
  return static_cast<int32_t>(read_u32(pc));
}

uint16_t read_u16(const uint8_t *&pc) {
  uint16_t v = static_cast<uint16_t>(
      static_cast<unsigned>(pc[0]) | (static_cast<unsigned>(pc[1]) << 8));
  pc += 2;
  return v;
}

uint8_t read_u8(const uint8_t *&pc) {
  return *pc++;
}

Atom read_atom(const uint8_t *&pc) {
  return static_cast<Atom>(read_u32(pc));
}

// ─── Label resolution ───────────────────────────────────────────────────────

void resolve_labels(FunctionBytecode *b, const FunctionDef *fd) {
  // Pass 1: locate all OP_label positions
  std::vector<int> label_positions(static_cast<size_t>(fd->label_count), -1);
  const uint8_t *pc = b->byte_code_buf;
  const uint8_t *end = pc + b->byte_code_len;

  while (pc < end) {
    uint8_t op = *pc;
    if (op == OP_label) {
      const uint8_t *lp = pc + 1;
      uint32_t idx = read_u32(lp);
      if (idx < static_cast<uint32_t>(fd->label_count))
        label_positions[idx] = static_cast<int>(pc - b->byte_code_buf);
    }
    pc += kOpInfo[op].size;
  }

  // Pass 2: rewrite jump targets (label indices → bytecode offsets)
  pc = b->byte_code_buf;
  while (pc < end) {
    uint8_t op = *pc;
    if (op == OP_if_false || op == OP_if_true || op == OP_goto ||
        op == OP_catch || op == OP_gosub) {
      const uint8_t *lp = pc + 1;
      uint32_t idx = (static_cast<uint32_t>(lp[0]) |
                      (static_cast<uint32_t>(lp[1]) << 8) |
                      (static_cast<uint32_t>(lp[2]) << 16) |
                      (static_cast<uint32_t>(lp[3]) << 24));
      if (idx < static_cast<uint32_t>(fd->label_count) &&
          label_positions[idx] >= 0) {
        int target = label_positions[idx];
        // Replace the 4-byte label index with the absolute bytecode offset
        uint8_t *mp = const_cast<uint8_t *>(lp);
        mp[0] = static_cast<uint8_t>(target);
        mp[1] = static_cast<uint8_t>(target >> 8);
        mp[2] = static_cast<uint8_t>(target >> 16);
        mp[3] = static_cast<uint8_t>(target >> 24);
      }
    }
    pc += kOpInfo[op].size;
  }

  // Pass 3: remove OP_label opcodes (they become no-ops or get stripped)
  // For simplicity, we keep them as OP_nop (opcode 260)
  // Actually, we leave them — the interpreter just treats OP_label as a marker
  // and jumps to the byte AFTER the OP_label + its operand.
  // In our resolve_labels, label_positions points to the OP_label byte,
  // and the jump target should point to AFTER OP_label + 4-byte operand.
  // Let me adjust: make label_positions point to after OP_label + operand.
  for (size_t i = 0; i < label_positions.size(); i++) {
    if (label_positions[i] >= 0)
      label_positions[i] += 5; // OP_label(1) + u32(4)
  }

  // Re-do pass 2 with corrected positions
  pc = b->byte_code_buf;
  while (pc < end) {
    uint8_t op = *pc;
    if (op == OP_if_false || op == OP_if_true || op == OP_goto ||
        op == OP_catch || op == OP_gosub) {
      const uint8_t *lp = pc + 1;
      uint32_t idx = (static_cast<uint32_t>(lp[0]) |
                      (static_cast<uint32_t>(lp[1]) << 8) |
                      (static_cast<uint32_t>(lp[2]) << 16) |
                      (static_cast<uint32_t>(lp[3]) << 24));
      if (idx < static_cast<uint32_t>(fd->label_count) &&
          label_positions[idx] >= 0) {
        int target = label_positions[idx];
        uint8_t *mp = const_cast<uint8_t *>(lp);
        mp[0] = static_cast<uint8_t>(target);
        mp[1] = static_cast<uint8_t>(target >> 8);
        mp[2] = static_cast<uint8_t>(target >> 16);
        mp[3] = static_cast<uint8_t>(target >> 24);
      }
    }
    pc += kOpInfo[op].size;
  }

  // Convert OP_label opcodes to OP_nop (and fill operand bytes)
  pc = b->byte_code_buf;
  while (pc < end) {
    uint8_t op = *pc;
    int sz = kOpInfo[op].size;
    if (op == OP_label) {
      for (int i = 0; i < sz; i++)
        const_cast<uint8_t *>(pc)[i] = OP_nop;
    }
    pc += sz;
  }
}

// ─── Lower FunctionDef → FunctionBytecode ───────────────────────────────────

static FunctionBytecode *lower(FunctionDef *fd) {
  auto *b = new FunctionBytecode();
  b->ref_count = 1;
  b->gc_obj_type = GCObjType::function_bytecode;

  // Copy bytecode
  b->byte_code_len = static_cast<int>(fd->byte_code.size());
  b->byte_code_buf = new uint8_t[static_cast<size_t>(b->byte_code_len)];
  std::memcpy(b->byte_code_buf, fd->byte_code.data(),
              static_cast<size_t>(b->byte_code_len));

  // Copy constant pool
  b->cpool_count = static_cast<int>(fd->cpool.size());
  b->cpool = new Value[static_cast<size_t>(b->cpool_count)];
  for (int i = 0; i < b->cpool_count; i++)
    b->cpool[i] = fd->cpool[static_cast<size_t>(i)];

  b->arg_count = static_cast<uint16_t>(fd->arg_count);
  b->var_count = static_cast<uint16_t>(fd->var_count);
  b->defined_arg_count = static_cast<uint16_t>(fd->defined_arg_count);
  b->stack_size = static_cast<uint16_t>(
      std::max(fd->arg_count + fd->var_count + 64, 256));

  b->func_name = fd->func_name;
  b->js_mode = fd->js_mode;
  b->flags1 = 0;
  b->flags2 = 0;

  if (fd->has_prototype) b->flags1 |= 0x01;
  if (fd->has_simple_parameter_list) b->flags1 |= 0x02;
  if (fd->is_derived_class_constructor) b->flags1 |= 0x04;
  if (fd->need_home_object) b->flags1 |= 0x08;

  uint8_t fk = 0;
  switch (fd->func_kind) {
  case FunctionKind::normal: fk = 0; break;
  case FunctionKind::generator: fk = 1; break;
  case FunctionKind::async: fk = 2; break;
  case FunctionKind::async_generator: fk = 3; break;
  }
  b->flags1 |= (fk << 4);
  if (fd->new_target_allowed) b->flags1 |= 0x40;
  if (fd->super_call_allowed) b->flags1 |= 0x80;

  if (fd->super_allowed) b->flags2 |= 0x01;
  if (fd->arguments_allowed) b->flags2 |= 0x02;
  if (fd->has_eval_call) b->flags2 |= 0x10;

  // Var definitions (includes both args and vars)
  int total_defs = fd->arg_count + fd->var_count;
  if (total_defs > 0) {
    b->vardefs = new BytecodeVarDef[static_cast<size_t>(total_defs)];
    // Args come first
    for (int i = 0; i < fd->arg_count; i++) {
      auto &vd = fd->args[static_cast<size_t>(i)];
      b->vardefs[i].var_name = vd.var_name;
      b->vardefs[i].scope_next = vd.scope_next;
      b->vardefs[i].flags = 0;
      b->vardefs[i].var_ref_idx = 0;
    }
    // Then vars
    for (int i = 0; i < fd->var_count; i++) {
      auto &vd = fd->vars[static_cast<size_t>(i)];
      int idx = fd->arg_count + i;
      b->vardefs[idx].var_name = vd.var_name;
      b->vardefs[idx].scope_next = vd.scope_next;
      b->vardefs[idx].flags = 0;
      b->vardefs[idx].set_is_const(vd.is_const);
      b->vardefs[idx].set_is_lexical(vd.is_lexical);
      b->vardefs[idx].set_is_captured(vd.is_captured);
      b->vardefs[idx].set_var_kind(vd.var_kind);
      b->vardefs[idx].var_ref_idx = 0;
    }
    b->var_count = static_cast<uint16_t>(total_defs);
  }

  // Resolve labels
  resolve_labels(b, fd);

  // Lower child FunctionDefs and resolve cpool entries
  for (size_t ci = 0; ci < fd->children.size(); ci++) {
    FunctionDef *child_fd = fd->children[ci];
    FunctionBytecode *child_b = lower(child_fd);

    // Find the corresponding cpool entry (it's the placeholder we added)
    for (int i = 0; i < b->cpool_count; i++) {
      if (b->cpool[i].is_func_bytecode() &&
          b->cpool[i].as<FunctionBytecode>() == nullptr) {
        b->cpool[i] = Value::func_bytecode(child_b);
        break;
      }
    }
  }

  return b;
}

// ─── Variable resolution (minimum: globals via context's global object) ─────

static Object *global_obj(Context *ctx) {
  return ctx->global_obj.as<Object>();
}

Value Interpreter::get_var(Atom name) {
  // Check locals first (if we're in a function with var_buf and vardefs)
  StackFrame *sf = current_frame_;
  if (sf && sf->b && sf->var_buf && sf->b->vardefs) {
    for (int i = 0; i < sf->b->var_count; i++) {
      if (sf->b->vardefs[i].var_name == name)
        return sf->var_buf[i];
    }
  }
  // Fall through to global
  auto *g = global_obj(ctx_);
  if (g) return g->get(name);
  return kUndefined;
}

void Interpreter::put_var(Atom name, Value val, bool /*init*/) {
  StackFrame *sf = current_frame_;
  if (sf && sf->b && sf->var_buf && sf->b->vardefs) {
    for (int i = 0; i < sf->b->var_count; i++) {
      if (sf->b->vardefs[i].var_name == name) {
        sf->var_buf[i] = val;
        return;
      }
    }
  }
  auto *g = global_obj(ctx_);
  if (g) g->set_own(rt(), name, val);
}

Value Interpreter::get_field(Value obj, Atom name) {
  if (obj.is_object()) {
    auto *o = obj.as<Object>();
    return o ? o->get(name) : kUndefined;
  }
  return kUndefined;
}

void Interpreter::put_field(Value obj, Atom name, Value val) {
  if (obj.is_object()) {
    auto *o = obj.as<Object>();
    if (o) o->set_own(rt(), name, val);
  }
}

// ─── Run bytecode ───────────────────────────────────────────────────────────

Value Interpreter::run_bytecode(FunctionBytecode *b, StackFrame *sf) {
  const uint8_t *pc = sf->pc;
  Value *sp = sf->sp;

  // Opcode dispatch loop
  for (;;) {
    uint8_t op = *pc++;
    switch (op) {

    // ── push values ───────────────────────────────────────────────────
    case OP_push_i32: {
      int32_t v = read_i32(pc);
      *sp++ = Value::int32(v);
      break;
    }
    case OP_push_const: {
      uint32_t idx = read_u32(pc);
      if (idx < static_cast<uint32_t>(b->cpool_count))
        *sp++ = b->cpool[idx];
      else
        *sp++ = kUndefined;
      break;
    }
    case OP_push_atom_value: {
      Atom atom = read_atom(pc);
      // Atom → String → Value
      String *s = rt()->atom_to_string(atom);
      if (s) {
        s->dup();
        *sp++ = Value::string(s);
      } else {
        *sp++ = kUndefined;
      }
      break;
    }
    case OP_undefined:
      *sp++ = kUndefined;
      break;
    case OP_null:
      *sp++ = kNull;
      break;
    case OP_push_this:
      *sp++ = sf->this_obj;
      break;
    case OP_push_false:
      *sp++ = kFalse;
      break;
    case OP_push_true:
      *sp++ = kTrue;
      break;
    case OP_object:
      *sp++ = Value::object(
          Object::create(rt(), nullptr,
                         static_cast<int>(ClassID::object)));
      break;

    case OP_fclosure: {
      uint32_t idx = read_u32(pc);
      if (idx < static_cast<uint32_t>(b->cpool_count) &&
          b->cpool[idx].is_func_bytecode()) {
        // Create a closure object from the FunctionBytecode
        auto *inner = b->cpool[idx].as<FunctionBytecode>();
        auto *fn = Object::create(rt(), nullptr,
                                  static_cast<int>(ClassID::bytecode_function));
        fn->u.opaque = inner; // store bytecode reference
        *sp++ = Value::object(fn);
      } else {
        *sp++ = kUndefined;
      }
      break;
    }

    // ── arithmetic ────────────────────────────────────────────────────
    case OP_add: {
      Value r = *--sp;
      Value l = *--sp;
      if (l.is_int32() && r.is_int32()) {
        int64_t result = static_cast<int64_t>(l.as_int32()) + static_cast<int64_t>(r.as_int32());
        if (result >= -2147483648LL && result <= 2147483647LL)
          *sp++ = Value::int32(static_cast<int32_t>(result));
        else
          *sp++ = Value::float64(static_cast<double>(result));
      } else if (l.is_number() && r.is_number()) {
        double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
        double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
        *sp++ = Value::float64(lv + rv);
      } else {
        *sp++ = Value::float64(0.0); // placeholder
      }
      break;
    }
    case OP_sub: {
      Value r = *--sp;
      Value l = *--sp;
      if (l.is_int32() && r.is_int32()) {
        int64_t result = static_cast<int64_t>(l.as_int32()) - static_cast<int64_t>(r.as_int32());
        if (result >= -2147483648LL && result <= 2147483647LL)
          *sp++ = Value::int32(static_cast<int32_t>(result));
        else
          *sp++ = Value::float64(static_cast<double>(result));
      } else {
        double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
        double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
        *sp++ = Value::float64(lv - rv);
      }
      break;
    }
    case OP_mul: {
      Value r = *--sp;
      Value l = *--sp;
      if (l.is_int32() && r.is_int32()) {
        int64_t result = static_cast<int64_t>(l.as_int32()) * static_cast<int64_t>(r.as_int32());
        if (result >= -2147483648LL && result <= 2147483647LL)
          *sp++ = Value::int32(static_cast<int32_t>(result));
        else
          *sp++ = Value::float64(static_cast<double>(result));
      } else {
        double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
        double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
        *sp++ = Value::float64(lv * rv);
      }
      break;
    }
    case OP_div: {
      Value r = *--sp;
      Value l = *--sp;
      double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
      double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
      *sp++ = Value::float64(lv / rv);
      break;
    }
    case OP_mod: {
      Value r = *--sp;
      Value l = *--sp;
      int32_t lv = l.is_int32() ? l.as_int32() : static_cast<int32_t>(l.as_double());
      int32_t rv = r.is_int32() ? r.as_int32() : static_cast<int32_t>(r.as_double());
      if (rv != 0)
        *sp++ = Value::int32(lv % rv);
      else
        *sp++ = Value::float64(NAN);
      break;
    }
    case OP_neg: {
      Value v = *--sp;
      double dv = v.is_int32() ? static_cast<double>(v.as_int32()) : v.as_double();
      *sp++ = Value::float64(-dv);
      break;
    }

    // ── comparison ────────────────────────────────────────────────────
    case OP_eq:
    case OP_neq:
    case OP_strict_eq:
    case OP_strict_neq:
    case OP_lt:
    case OP_gt:
    case OP_lte:
    case OP_gte: {
      Value r = *--sp;
      Value l = *--sp;
      double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
      double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
      bool result = false;
      switch (op) {
      case OP_eq: result = (lv == rv); break;
      case OP_neq: result = (lv != rv); break;
      case OP_strict_eq: result = (lv == rv); break;
      case OP_strict_neq: result = (lv != rv); break;
      case OP_lt: result = (lv < rv); break;
      case OP_gt: result = (lv > rv); break;
      case OP_lte: result = (lv <= rv); break;
      case OP_gte: result = (lv >= rv); break;
      }
      *sp++ = Value::bool_(result);
      break;
    }

    // ── bitwise / logical ─────────────────────────────────────────────
    case OP_not: {
      Value v = *--sp;
      int32_t iv = v.is_int32() ? v.as_int32() : static_cast<int32_t>(v.as_double());
      *sp++ = Value::int32(~iv);
      break;
    }
    case OP_lnot: {
      Value v = *--sp;
      bool truthy = !v.is_null() &&
                    !v.is_undefined() &&
                    !(v.is_bool() && !v.as_bool()) &&
                    !(v.is_int32() && v.as_int32() == 0) &&
                    !(v.is_double() && v.as_double() == 0.0);
      *sp++ = Value::bool_(!truthy);
      break;
    }
    case OP_and: case OP_xor: case OP_or: {
      Value r = *--sp;
      Value l = *--sp;
      int32_t lv = l.is_int32() ? l.as_int32() : static_cast<int32_t>(l.as_double());
      int32_t rv = r.is_int32() ? r.as_int32() : static_cast<int32_t>(r.as_double());
      int32_t result = 0;
      if (op == OP_and) result = lv & rv;
      else if (op == OP_xor) result = lv ^ rv;
      else result = lv | rv;
      *sp++ = Value::int32(result);
      break;
    }
    case OP_shl: case OP_sar: case OP_shr: {
      Value r = *--sp;
      Value l = *--sp;
      int32_t lv = l.is_int32() ? l.as_int32() : static_cast<int32_t>(l.as_double());
      int32_t rv = r.is_int32() ? r.as_int32() : static_cast<int32_t>(r.as_double());
      int32_t result = 0;
      if (op == OP_shl) result = lv << (rv & 31);
      else if (op == OP_sar) result = lv >> (rv & 31);
      else result = static_cast<int32_t>(static_cast<uint32_t>(lv) >> (rv & 31));
      *sp++ = Value::int32(result);
      break;
    }
    case OP_pow: {
      Value r = *--sp;
      Value l = *--sp;
      double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
      double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
      *sp++ = Value::float64(std::pow(lv, rv));
      break;
    }

    // ── typeof / instanceof / in ──────────────────────────────────────
    case OP_typeof: {
      Value v = *--sp;
      // Simplified typeof
      if (v.is_int32() || v.is_double()) {
        auto *s = String::create("number"); s->dup();
        *sp++ = Value::string(s);
      } else if (v.is_string()) {
        auto *s = String::create("string"); s->dup();
        *sp++ = Value::string(s);
      } else if (v.is_object()) {
        auto *s = String::create("object"); s->dup();
        *sp++ = Value::string(s);
      } else if (v.is_bool()) {
        auto *s = String::create("boolean"); s->dup();
        *sp++ = Value::string(s);
      } else if (v.is_undefined()) {
        auto *s = String::create("undefined"); s->dup();
        *sp++ = Value::string(s);
      } else {
        auto *s = String::create("object"); s->dup();
        *sp++ = Value::string(s);
      }
      break;
    }
    case OP_instanceof:
      *sp++ = kFalse; // simplified
      break;
    case OP_in:
      *sp++ = kFalse; // simplified
      break;
    case OP_plus: {
      Value v = *--sp;
      double dv = v.is_int32() ? static_cast<double>(v.as_int32()) : v.as_double();
      *sp++ = Value::float64(+dv);
      break;
    }

    // ── stack manipulation ────────────────────────────────────────────
    case OP_drop: --sp; break;
    case OP_nip: { sp[-2] = sp[-1]; --sp; break; }
    case OP_dup: sp[0] = sp[-1]; sp++; break;
    case OP_swap: { Value t = sp[-1]; sp[-1] = sp[-2]; sp[-2] = t; break; }
    case OP_insert2: /* a b → b a b */
      sp[0] = sp[-2]; sp[1] = sp[-1]; sp[2] = sp[-1]; sp += 3;
      sp[-3] = sp[-5]; sp[-4] = sp[-6]; break;
    // Simplified insert2: just dup
    case OP_nop: break;
    case OP_line_num: read_u32(pc); break; // ignore

    // ── scope / variable access ───────────────────────────────────────
    case OP_scope_get_var: {
      Atom name = read_atom(pc);
      uint16_t scope = read_u16(pc);
      (void)scope;
      *sp++ = get_var(name);
      break;
    }
    case OP_scope_put_var:
    case OP_scope_put_var_init: {
      Atom name = read_atom(pc);
      uint16_t scope = read_u16(pc);
      (void)scope;
      Value val = *--sp;
      put_var(name, val, op == OP_scope_put_var_init);
      break;
    }
    case OP_get_field: {
      Atom name = read_atom(pc);
      Value obj = *--sp;
      *sp++ = get_field(obj, name);
      break;
    }
    case OP_get_field2: {
      Atom name = read_atom(pc);
      Value obj = sp[-1];
      sp[0] = get_field(obj, name);
      sp++;
      break;
    }
    case OP_put_field: {
      Atom name = read_atom(pc);
      Value val = *--sp;
      Value obj = *--sp;
      put_field(obj, name, val);
      break;
    }
    case OP_define_field: {
      Atom name = read_atom(pc);
      Value val = *--sp;
      Value obj = *--sp;
      put_field(obj, name, val);
      *sp++ = obj; // define_field leaves obj on stack
      break;
    }
    case OP_get_array_el: {
      Value prop = *--sp;
      Value obj = *--sp;
      Atom name = kAtomNull;
      if (prop.is_string()) {
        auto *s = prop.as<String>();
        name = rt()->intern(s);
      } else if (prop.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", prop.as_int32());
        name = rt()->intern(String::create(buf));
      }
      *sp++ = get_field(obj, name);
      break;
    }
    case OP_put_array_el: {
      Value val = *--sp;
      Value prop = *--sp;
      Value obj = *--sp;
      Atom name = kAtomNull;
      if (prop.is_string()) {
        name = rt()->intern(prop.as<String>());
      } else if (prop.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", prop.as_int32());
        name = rt()->intern(String::create(buf));
      }
      put_field(obj, name, val);
      break;
    }
    case OP_define_array_el: {
      Value val = *--sp;
      Value prop = *--sp;
      Value obj = *--sp;
      Atom name = kAtomNull;
      if (prop.is_string()) {
        name = rt()->intern(prop.as<String>());
      } else if (prop.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", prop.as_int32());
        name = rt()->intern(String::create(buf));
      }
      put_field(obj, name, val);
      *sp++ = val; // define_array_el leaves val on stack? No, it increments idx
      *sp++ = Value::int32(prop.is_int32() ? prop.as_int32() + 1 : 0);
      break;
    }
    case OP_to_propkey: {
      // Convert top of stack to property key (atom or string)
      break;
    }
    case OP_set_proto: {
      Value proto = *--sp;
      Value obj = sp[-1]; // peek
      if (obj.is_object() && proto.is_object()) {
        obj.as<Object>()->proto = proto.as<Object>();
      }
      break;
    }

    // ── control flow ──────────────────────────────────────────────────
    case OP_if_false:
    case OP_if_true: {
      int32_t target = read_i32(pc);
      Value cond = *--sp;
      bool truthy = !cond.is_null() &&
                    !cond.is_undefined() &&
                    !(cond.is_bool() && !cond.as_bool()) &&
                    !(cond.is_int32() && cond.as_int32() == 0) &&
                    !(cond.is_double() && cond.as_double() == 0.0);
      bool jump = (op == OP_if_false) ? !truthy : truthy;
      if (jump && target >= 0) {
        pc = b->byte_code_buf + target;
      }
      break;
    }
    case OP_goto: {
      int32_t target = read_i32(pc);
      if (target >= 0) {
        pc = b->byte_code_buf + target;
      }
      break;
    }
    case OP_label:
      read_u32(pc); // skip label index, should already be OP_nop
      break;

    // ── calls ─────────────────────────────────────────────────────────
    case OP_call: {
      uint16_t argc = read_u16(pc);
      Value func = *(sp - argc - 1);
      Value this_val = kUndefined;

      Value ret;
      if (func.is_object()) {
        auto *obj = func.as<Object>();
        if (obj->class_id == static_cast<uint16_t>(ClassID::c_function) &&
            obj->u.cfunc.fn) {
          ret = obj->u.cfunc.fn(ctx_, this_val, argc, sp - argc);
        } else if (obj->class_id == static_cast<uint16_t>(ClassID::bytecode_function) &&
                   obj->u.opaque) {
          auto *inner = static_cast<FunctionBytecode *>(obj->u.opaque);
          ret = call_bytecode(inner, this_val, argc, sp - argc);
        } else {
          ret = kUndefined;
        }
      } else {
        ret = kUndefined;
      }

      // Pop arguments + function, push result
      sp -= argc + 1;
      *sp++ = ret;
      break;
    }
    case OP_call_method: {
      uint16_t argc = read_u16(pc);
      Value func = *(sp - argc - 2);
      Value this_val = *(sp - argc - 1);
      Value ret = kUndefined;
      if (func.is_object()) {
        auto *obj = func.as<Object>();
        if (obj->class_id == static_cast<uint16_t>(ClassID::c_function) &&
            obj->u.cfunc.fn) {
          ret = obj->u.cfunc.fn(ctx_, this_val, argc, sp - argc);
        }
      }
      sp -= argc + 2;
      *sp++ = ret;
      break;
    }

    // ── array ─────────────────────────────────────────────────────────
    case OP_array_from: {
      uint16_t count = read_u16(pc);
      auto *arr = Object::create(rt(), nullptr,
                                 static_cast<int>(ClassID::array));
      for (uint16_t i = 0; i < count; i++) {
        Value elem = *(sp - count + i);
        char buf[32];
        snprintf(buf, sizeof(buf), "%u", i);
        Atom key = rt()->intern(String::create(buf));
        arr->set_own(rt(), key, elem);
      }
      sp -= count;
      *sp++ = Value::object(arr);
      break;
    }

    // ── scope enter/leave ─────────────────────────────────────────────
    case OP_enter_scope:
      read_u16(pc); // ignore scope index for now
      break;
    case OP_leave_scope:
      read_u16(pc);
      break;

    // ── inc/dec ───────────────────────────────────────────────────────
    case OP_inc: {
      Value v = *--sp;
      double dv = v.is_int32() ? static_cast<double>(v.as_int32()) : v.as_double();
      *sp++ = Value::float64(dv + 1.0);
      break;
    }
    case OP_dec: {
      Value v = *--sp;
      double dv = v.is_int32() ? static_cast<double>(v.as_int32()) : v.as_double();
      *sp++ = Value::float64(dv - 1.0);
      break;
    }
    case OP_post_inc: {
      Value v = *--sp;
      double dv = v.is_int32() ? static_cast<double>(v.as_int32()) : v.as_double();
      *sp++ = Value::float64(dv);
      *sp++ = Value::float64(dv + 1.0);
      break;
    }
    case OP_post_dec: {
      Value v = *--sp;
      double dv = v.is_int32() ? static_cast<double>(v.as_int32()) : v.as_double();
      *sp++ = Value::float64(dv);
      *sp++ = Value::float64(dv - 1.0);
      break;
    }

    // ── copy_data_properties (spread) ─────────────────────────────────
    case OP_copy_data_properties: {
      read_u8(pc); // flags
      --sp; // pop src
      --sp; // pop excludeList
      Value target = *--sp;
      // Simplified: copy properties from src to target
      // Just pop and push target back
      *sp++ = target;
      break;
    }

    // ── append ────────────────────────────────────────────────────────
    case OP_append: {
      Value val = *--sp;
      Value obj = *--sp;
      if (obj.is_object()) {
        auto *arr = obj.as<Object>();
        if (arr) {
          int len = 0;
          if (arr->shape) {
            for (auto &p : arr->properties) { (void)p; len++; }
          }
          char buf[32];
          snprintf(buf, sizeof(buf), "%d", len);
          Atom key = rt()->intern(String::create(buf));
          arr->set_own(rt(), key, val);
        }
      }
      *sp++ = obj;
      break;
    }

    // ── return ────────────────────────────────────────────────────────
    case OP_return: {
      sf->pc = pc;
      sf->sp = sp;
      return *--sp;
    }
    case OP_return_undef: {
      sf->pc = pc;
      sf->sp = sp;
      return kUndefined;
    }
    case OP_throw: {
      Value v = *--sp;
      // Simplified: just return the exception value
      sf->pc = pc;
      sf->sp = sp;
      return v;
    }
    case OP_throw_error: {
      read_atom(pc);
      read_u8(pc);
      return kException;
    }

    default:
      fprintf(stderr, "Unhandled opcode: %u\n", op);
      sf->pc = pc;
      sf->sp = sp;
      return kUndefined;
    }
  }
}

// ─── Call internal ──────────────────────────────────────────────────────────

Value Interpreter::call_bytecode(FunctionBytecode *b, Value this_obj,
                                  int argc, Value *argv) {
  // Allocate space for local variables
  int total_vars = b->var_count;
  int total_args = b->arg_count;

  // Find available space on the operand stack
  Value *var_start = current_frame_ ? current_frame_->sp : stack_;
  Value *sp = var_start;

  // Store 'this'
  *sp++ = this_obj;

  // Store arguments
  for (int i = 0; i < argc; i++)
    *sp++ = argv[i];

  // Fill missing args with undefined
  for (int i = argc; i < total_args; i++)
    *sp++ = kUndefined;

  // Initialize local variables to undefined
  for (int i = total_args; i < total_vars; i++)
    *sp++ = kUndefined;

  // Set up stack frame
  StackFrame sf;
  sf.b = b;
  sf.pc = b->byte_code_buf;
  sf.sp = sp;
  sf.var_buf = var_start + 1; // skip 'this'
  sf.arg_buf = var_start + 1;
  sf.this_obj = this_obj;
  sf.prev_frame = current_frame_;

  StackFrame *prev = current_frame_;
  current_frame_ = &sf;

  Value result = run_bytecode(b, &sf);

  current_frame_ = prev;
  return result;
}

// ─── Eval ───────────────────────────────────────────────────────────────────

Value Interpreter::eval(FunctionBytecode *b) {
  if (!b) return kUndefined;

  current_frame_ = nullptr;

  // Create a wrapper around the bytecode
  int total_vars = b->var_count;
  Value *sp = stack_;

  // 'this' = undefined (top-level)
  *sp++ = kUndefined;

  // Initialize local vars
  for (int i = 0; i < total_vars; i++)
    *sp++ = kUndefined;

  StackFrame sf;
  sf.b = b;
  sf.pc = b->byte_code_buf;
  sf.sp = sp;
  sf.var_buf = stack_ + 1;
  sf.arg_buf = stack_ + 1;
  sf.this_obj = kUndefined;
  sf.prev_frame = nullptr;

  current_frame_ = &sf;
  return run_bytecode(b, &sf);
}

Value Interpreter::eval_source(const char *source, const char *filename) {
  // Lex + Parse
  ParseState ps(rt(), ctx_);
  ps.init(source, filename);

  if (!ps.compile()) {
    fprintf(stderr, "Compilation failed\n");
    return kException;
  }

  // Lower FunctionDef → FunctionBytecode
  auto *b = lower(ps.cur_func);
  if (!b) return kException;

  // Interpret
  Value result = eval(b);

  // Cleanup
  delete[] b->byte_code_buf;
  delete[] b->cpool;
  delete[] b->vardefs;
  delete b;

  return result;
}

} // namespace qjsp
