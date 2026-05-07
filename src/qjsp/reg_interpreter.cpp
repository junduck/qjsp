#include "qjsp/reg_interpreter.hpp"
#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/reg_opcode.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace qjsp {

Runtime *RegInterpreter::rt() const { return ctx_->rt; }

Object *RegInterpreter::global_obj() const {
  return ctx_->global_obj.as<Object>();
}

// ─── Value helpers ──────────────────────────────────────────────────────────

static bool is_truthy(Value v) {
  if (v.is_null() || v.is_undefined()) return false;
  if (v.is_bool()) return v.as_bool();
  if (v.is_int32()) return v.as_int32() != 0;
  if (v.is_double()) return v.as_double() != 0.0;
  return true;
}

static Value add_values(Value l, Value r) {
  if (l.is_int32() && r.is_int32()) {
    int64_t result = static_cast<int64_t>(l.as_int32()) +
                     static_cast<int64_t>(r.as_int32());
    if (result >= -2147483648LL && result <= 2147483647LL)
      return Value::int32(static_cast<int32_t>(result));
    return Value::float64(static_cast<double>(result));
  }
  double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
  double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
  return Value::float64(lv + rv);
}

static Value sub_values(Value l, Value r) {
  if (l.is_int32() && r.is_int32()) {
    int64_t result = static_cast<int64_t>(l.as_int32()) -
                     static_cast<int64_t>(r.as_int32());
    if (result >= -2147483648LL && result <= 2147483647LL)
      return Value::int32(static_cast<int32_t>(result));
    return Value::float64(static_cast<double>(result));
  }
  double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
  double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
  return Value::float64(lv - rv);
}

static Value mul_values(Value l, Value r) {
  if (l.is_int32() && r.is_int32()) {
    int64_t result = static_cast<int64_t>(l.as_int32()) *
                     static_cast<int64_t>(r.as_int32());
    if (result >= -2147483648LL && result <= 2147483647LL)
      return Value::int32(static_cast<int32_t>(result));
    return Value::float64(static_cast<double>(result));
  }
  double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
  double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
  return Value::float64(lv * rv);
}

static Value div_values(Value l, Value r) {
  double lv = l.is_int32() ? static_cast<double>(l.as_int32()) : l.as_double();
  double rv = r.is_int32() ? static_cast<double>(r.as_int32()) : r.as_double();
  return Value::float64(lv / rv);
}

static Value mod_values(Value l, Value r) {
  int32_t lv = l.is_int32() ? l.as_int32()
                           : static_cast<int32_t>(l.as_double());
  int32_t rv = r.is_int32() ? r.as_int32()
                           : static_cast<int32_t>(r.as_double());
  if (rv != 0) return Value::int32(lv % rv);
  return Value::float64(NAN);
}

// ─── Field access ───────────────────────────────────────────────────────────

Value RegInterpreter::get_field(Value obj, Atom name) {
  if (obj.is_object()) {
    auto *o = obj.as<Object>();
    if (o) return o->get(name);
  }
  return kUndefined;
}

void RegInterpreter::put_field(Value obj, Atom name, Value val) {
  if (obj.is_object()) {
    auto *o = obj.as<Object>();
    if (o) o->set_own(rt(), name, val);
  }
}

// ─── Run bytecode ───────────────────────────────────────────────────────────

Value RegInterpreter::run_bytecode(FunctionBytecode *b, Value *regs,
                                    VarRef **upvals) {
  const auto *ip = reinterpret_cast<const Instruction *>(b->byte_code_buf);
  const auto *end = reinterpret_cast<const Instruction *>(
      b->byte_code_buf + b->instr_count * 4);

  while (ip < end) {
    Instruction i = *ip;
    RegOp op = static_cast<RegOp>(i.opcode());
    ip++;

    switch (op) {

    // ── const / move ────────────────────────────────────────────────────────

    case RegOp::NOP:
      break;

    case RegOp::LOADK:
      if (i.bx() < static_cast<uint32_t>(b->cpool_count))
        regs[i.a()] = b->cpool[i.bx()];
      else
        regs[i.a()] = kUndefined;
      break;

    case RegOp::LOADINT:
      regs[i.a()] = Value::int32(i.sbx());
      break;

    case RegOp::LOADUNDEF:
      regs[i.a()] = kUndefined;
      break;

    case RegOp::LOADNULL:
      regs[i.a()] = kNull;
      break;

    case RegOp::LOADTRUE:
      regs[i.a()] = kTrue;
      break;

    case RegOp::LOADFALSE:
      regs[i.a()] = kFalse;
      break;

    case RegOp::LOADNIL:
      // R[A]..R[A+B] = undefined
      for (int r = i.a(); r <= i.a() + i.b(); r++)
        regs[r] = kUndefined;
      break;

    case RegOp::LOADBOOL:
      regs[i.a()] = Value::bool_(i.b() != 0);
      if (i.c()) ip++; // skip next instruction
      break;

    case RegOp::MOVE:
      regs[i.a()] = regs[i.b()];
      break;

    // ── arithmetic ──────────────────────────────────────────────────────────

    case RegOp::ADD:
      regs[i.a()] = add_values(regs[i.b()], regs[i.c()]);
      break;
    case RegOp::SUB:
      regs[i.a()] = sub_values(regs[i.b()], regs[i.c()]);
      break;
    case RegOp::MUL:
      regs[i.a()] = mul_values(regs[i.b()], regs[i.c()]);
      break;
    case RegOp::DIV:
      regs[i.a()] = div_values(regs[i.b()], regs[i.c()]);
      break;
    case RegOp::MOD:
      regs[i.a()] = mod_values(regs[i.b()], regs[i.c()]);
      break;
    case RegOp::POW:
      {
        double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
        double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
        regs[i.a()] = Value::float64(std::pow(lv, rv));
      }
      break;

    // ── bitwise ─────────────────────────────────────────────────────────────

    case RegOp::AND: {
      int32_t l = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l & r);
      break;
    }
    case RegOp::OR: {
      int32_t l = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l | r);
      break;
    }
    case RegOp::XOR: {
      int32_t l = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l ^ r);
      break;
    }
    case RegOp::SHL: {
      int32_t l = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l << (r & 31));
      break;
    }
    case RegOp::SAR: {
      int32_t l = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l >> (r & 31));
      break;
    }
    case RegOp::SHR: {
      int32_t l = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(static_cast<int32_t>(static_cast<uint32_t>(l) >> (static_cast<uint32_t>(r) & 31)));
      break;
    }

    // ── unary ───────────────────────────────────────────────────────────────

    case RegOp::NEG: {
      double v = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      regs[i.a()] = Value::float64(-v);
      break;
    }
    case RegOp::BNOT: {
      int32_t v = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      regs[i.a()] = Value::int32(~v);
      break;
    }
    case RegOp::LNOT:
      regs[i.a()] = Value::bool_(!is_truthy(regs[i.b()]));
      break;

    case RegOp::INC: {
      Value &v = regs[i.b()];
      if (v.is_int32()) regs[i.a()] = Value::int32(v.as_int32() + 1);
      else if (v.is_double()) regs[i.a()] = Value::float64(v.as_double() + 1.0);
      else { double dv = v.as_double(); regs[i.a()] = Value::float64(dv + 1.0); }
      break;
    }
    case RegOp::DEC: {
      Value &v = regs[i.b()];
      if (v.is_int32()) regs[i.a()] = Value::int32(v.as_int32() - 1);
      else if (v.is_double()) regs[i.a()] = Value::float64(v.as_double() - 1.0);
      else { double dv = v.as_double(); regs[i.a()] = Value::float64(dv - 1.0); }
      break;
    }

    // ── compare ─────────────────────────────────────────────────────────────

    case RegOp::EQ: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv == rv);
      break;
    }
    case RegOp::NEQ: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv != rv);
      break;
    }
    case RegOp::SEQ: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv == rv);
      break;
    }
    case RegOp::SNEQ: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv != rv);
      break;
    }
    case RegOp::LT: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv < rv);
      break;
    }
    case RegOp::GT: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv > rv);
      break;
    }
    case RegOp::LTE: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv <= rv);
      break;
    }
    case RegOp::GTE: {
      double lv = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv >= rv);
      break;
    }

    // ── control ─────────────────────────────────────────────────────────────

    case RegOp::JMP:
      ip += i.sbx();
      break;

    case RegOp::IS_FALSE:
      if (!is_truthy(regs[i.a()]))
        ip += i.sbx();
      break;

    case RegOp::IS_TRUE:
      if (is_truthy(regs[i.a()]))
        ip += i.sbx();
      break;

    // ── object ──────────────────────────────────────────────────────────────

    case RegOp::NEWOBJ: {
      auto *obj = Object::create(rt(), nullptr,
                                static_cast<int>(ClassID::object));
      regs[i.a()] = Value::object(obj);
      break;
    }

    case RegOp::GETFIELD: {
      // A = dst, B = obj, C = cpool index
      int ci = i.c();
      Value field_val = (ci < b->cpool_count) ? b->cpool[ci] : kUndefined;
      Atom atom = kAtomNull;
      if (field_val.is_string()) {
        atom = rt()->intern(field_val.as<String>());
      }
      regs[i.a()] = get_field(regs[i.b()], atom);
      break;
    }

    case RegOp::SETFIELD: {
      // A = obj, B = cpool index, C = val
      int ci = i.b();
      Value field_val = (ci < b->cpool_count) ? b->cpool[ci] : kUndefined;
      Atom atom = kAtomNull;
      if (field_val.is_string()) {
        atom = rt()->intern(field_val.as<String>());
      }
      put_field(regs[i.a()], atom, regs[i.c()]);
      break;
    }

    case RegOp::DEFINE_FIELD: {
      // A = obj, B = cpool index, C = val
      int ci = i.b();
      Value field_val = (ci < b->cpool_count) ? b->cpool[ci] : kUndefined;
      Atom atom = kAtomNull;
      if (field_val.is_string()) {
        atom = rt()->intern(field_val.as<String>());
      }
      put_field(regs[i.a()], atom, regs[i.c()]);
      // result stays in R[A] (= obj)
      break;
    }

    case RegOp::GETELEM: {
      // A = dst, B = obj, C = key
      Value &obj = regs[i.b()];
      Value &key = regs[i.c()];
      Atom atom = kAtomNull;
      if (key.is_string()) {
        atom = rt()->intern(key.as<String>());
      } else if (key.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", key.as_int32());
        atom = rt()->intern(String::create(buf));
      }
      regs[i.a()] = get_field(obj, atom);
      break;
    }

    case RegOp::SETELEM: {
      // A = obj, B = key, C = val
      Value &obj = regs[i.a()];
      Value &key = regs[i.b()];
      Atom atom = kAtomNull;
      if (key.is_string()) {
        atom = rt()->intern(key.as<String>());
      } else if (key.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", key.as_int32());
        atom = rt()->intern(String::create(buf));
      }
      put_field(obj, atom, regs[i.c()]);
      break;
    }

    case RegOp::DEFINE_ELEM: {
      // A = obj, B = key, C = val. Define property, increment key.
      Value &obj = regs[i.a()];
      Value &key = regs[i.b()];
      Atom atom = kAtomNull;
      if (key.is_string()) {
        atom = rt()->intern(key.as<String>());
      } else if (key.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", key.as_int32());
        atom = rt()->intern(String::create(buf));
      }
      put_field(obj, atom, regs[i.c()]);
      if (key.is_int32()) regs[i.b()] = Value::int32(key.as_int32() + 1);
      break;
    }

    // ── array ───────────────────────────────────────────────────────────────

    case RegOp::NEWARR: {
      auto *arr = Object::create(rt(), nullptr,
                                 static_cast<int>(ClassID::array));
      int base = i.b();
      int count = i.c();
      for (int idx = 0; idx < count; idx++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", idx);
        Atom key = rt()->intern(String::create(buf));
        arr->set_own(rt(), key, regs[base + idx]);
      }
      regs[i.a()] = Value::object(arr);
      break;
    }

    case RegOp::APPEND:
      // Placeholder
      break;

    // ── spread ──────────────────────────────────────────────────────────────

    case RegOp::SPREAD_OBJ:
    case RegOp::CALL_SPREAD:
      break;

    // ── type / conversion ───────────────────────────────────────────────────

    case RegOp::TYPEOF: {
      Value &v = regs[i.b()];
      const char *typestr = "object";
      if (v.is_int32() || v.is_double()) typestr = "number";
      else if (v.is_string()) typestr = "string";
      else if (v.is_bool()) typestr = "boolean";
      else if (v.is_undefined()) typestr = "undefined";
      auto *s = String::create(typestr);
      regs[i.a()] = Value::string(s);
      break;
    }

    case RegOp::TOPROPKEY:
      // Placeholder: pass through
      regs[i.a()] = regs[i.b()];
      break;

    case RegOp::SETPROTO:
      if (regs[i.a()].is_object() && regs[i.b()].is_object()) {
        regs[i.a()].as<Object>()->proto = regs[i.b()].as<Object>();
      }
      break;

    case RegOp::TOOBJECT:
      // Placeholder: if already object, pass through
      if (regs[i.b()].is_object())
        regs[i.a()] = regs[i.b()];
      else
        regs[i.a()] = kUndefined;
      break;

    // ── call / return ───────────────────────────────────────────────────────

    case RegOp::CALL: {
      int func_reg = i.b();
      int argc = i.c();
      Value func_val = regs[func_reg];

      if (func_val.is_object()) {
        auto *obj = func_val.as<Object>();
        if (obj->class_id == static_cast<uint16_t>(ClassID::c_function) &&
            obj->u.cfunc.fn) {
          regs[i.a()] = obj->u.cfunc.fn(ctx_, kUndefined, argc,
                                        &regs[func_reg + 1]);
        } else if (obj->class_id == static_cast<uint16_t>(ClassID::bytecode_function) &&
                   obj->u.opaque) {
          auto *inner = static_cast<FunctionBytecode *>(obj->u.opaque);
          regs[i.a()] = call_bytecode(inner, kUndefined, argc,
                                      &regs[func_reg + 1], nullptr);
        } else {
          regs[i.a()] = kUndefined;
        }
      } else {
        regs[i.a()] = kUndefined;
      }
      break;
    }

    case RegOp::CALL_M: {
      int func_reg = i.b();
      int argc = i.c();
      Value func_val = regs[func_reg];
      Value this_val = regs[func_reg + 1];

      if (func_val.is_object()) {
        auto *obj = func_val.as<Object>();
        if (obj->class_id == static_cast<uint16_t>(ClassID::c_function) &&
            obj->u.cfunc.fn) {
          regs[i.a()] = obj->u.cfunc.fn(ctx_, this_val, argc - 1,
                                        &regs[func_reg + 2]);
        } else {
          regs[i.a()] = kUndefined;
        }
      } else {
        regs[i.a()] = kUndefined;
      }
      break;
    }

    case RegOp::CTOR:
      // Placeholder: same as CALL for now
      regs[i.a()] = kUndefined;
      break;

    case RegOp::FCLOSURE: {
      // Load bytecode from cpool
      int ci = i.bx();
      if (ci < b->cpool_count && b->cpool[ci].is_func_bytecode()) {
        auto *inner_bc = b->cpool[ci].as<FunctionBytecode>();
        auto *closure = Object::create(rt(), nullptr,
                                       static_cast<int>(ClassID::bytecode_function));
        closure->u.opaque = inner_bc;
        regs[i.a()] = Value::object(closure);
      } else {
        regs[i.a()] = kUndefined;
      }
      break;
    }

    case RegOp::RETURN:
      return regs[i.a()];

    case RegOp::RETURN0:
      return kUndefined;

    // ── upvalue ─────────────────────────────────────────────────────────────

    case RegOp::GETUPVAL:
      if (upvals && upvals[i.b()]) {
        regs[i.a()] = upvals[i.b()]->load();
      } else {
        regs[i.a()] = kUndefined;
      }
      break;

    case RegOp::SETUPVAL:
      if (upvals && upvals[i.b()]) {
        upvals[i.b()]->store(regs[i.a()]);
      }
      break;

    case RegOp::CLOSEUPVAL:
      if (upvals && upvals[i.a()]) {
        upvals[i.a()]->close();
      }
      break;

    default:
      fprintf(stderr, "Unhandled reg opcode: %u\n",
              static_cast<unsigned>(op));
      return kUndefined;
    }
  }

  return kUndefined;
}

// ─── Call bytecode ──────────────────────────────────────────────────────────

Value RegInterpreter::call_bytecode(FunctionBytecode *b, Value this_obj,
                                     int argc, Value *argv,
                                     VarRef **upvals) {
  int total_regs = b->reg_count > 0 ? b->reg_count : 256;
  auto *regs = new Value[static_cast<size_t>(total_regs)]();

  // R[0] = this
  regs[0] = this_obj;

  // R[1]..R[argc] = arguments
  for (int i = 0; i < argc && i < b->arg_count; i++)
    regs[1 + i] = argv[i];

  // Unfilled args = undefined
  for (int i = argc; i < b->arg_count; i++)
    regs[1 + i] = kUndefined;

  // Local vars initialized to undefined
  for (int i = 0; i < b->var_count; i++)
    regs[1 + b->arg_count + i] = kUndefined;

  Value result = run_bytecode(b, regs, upvals);

  delete[] regs;
  return result;
}

// ─── Eval ───────────────────────────────────────────────────────────────────

Value RegInterpreter::eval(FunctionBytecode *b) {
  if (!b) return kUndefined;
  // Top-level eval: 'this' is the global object
  return call_bytecode(b, ctx_->global_obj, 0, nullptr, nullptr);
}

Value RegInterpreter::eval_source(const char *source, const char *filename) {
  // Parse → lower → execute
  RegParseState ps(rt(), ctx_);
  ps.init(source, filename);

  if (!ps.compile()) {
    fprintf(stderr, "Compilation failed\n");
    return kException;
  }

  auto *b = lower_reg(ps.cur_func, ctx_);
  if (!b) return kException;

  Value result = eval(b);

  // Cleanup
  delete[] b->byte_code_buf;
  delete[] b->cpool;
  delete[] b->vardefs;
  delete[] b->closure_var;
  delete b;

  // Cleanup FunctionDef tree
  delete ps.cur_func;

  return result;
}

} // namespace qjsp
