#include "qjsp/reg_interpreter.hpp"
#include "qjsp/array.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/object.hpp"
#include "qjsp/reg_opcode.hpp"
#include "qjsp/reg_opcode_info.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/regexp.hpp"
#include "qjsp/shape.hpp"
#include "qjsp/string.hpp"
#include "qjsp/varref.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace qjsp {

// ── BytecodeFunction virtuals ──────────────────────────────────────────────

Value BytecodeFunction::call(Engine *e, Value this_val, int argc, const Value *argv) {
  std::vector<VarRef *> upvals;
  upvals.reserve(var_refs.size());
  for (auto &v : var_refs)
    upvals.push_back(v.as<VarRef>());
  RegInterpreter interp{e};
  return interp.call_bytecode(bytecode, this_val, argc, argv, upvals.data());
}

Object *RegInterpreter::global_obj() const { return e_->global_obj.as<Object>(); }

// ─── Value helpers ──────────────────────────────────────────────────────────

static bool is_truthy(Value v) {
  if (v.is_null() || v.is_undefined())
    return false;
  if (v.is_bool())
    return v.as_bool();
  if (v.is_int32())
    return v.as_int32() != 0;
  if (v.is_double())
    return v.as_double() != 0.0;
  return true;
}

static Atom cpool_to_atom(Engine *e, Value field_val) {
  if (field_val.is_string())
    return e->intern(field_val.as<StrPrim>()->view());
  if (field_val.is_symbol())
    return field_val.as_symbol();
  return kAtomNull;
}

static Value add_values(Value l, Value r) {
  if (l.is_int32() && r.is_int32()) {
    int64_t result = static_cast<int64_t>(l.as_int32()) + static_cast<int64_t>(r.as_int32());
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
    int64_t result = static_cast<int64_t>(l.as_int32()) - static_cast<int64_t>(r.as_int32());
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
    int64_t result = static_cast<int64_t>(l.as_int32()) * static_cast<int64_t>(r.as_int32());
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
  int32_t lv = l.is_int32() ? l.as_int32() : static_cast<int32_t>(l.as_double());
  int32_t rv = r.is_int32() ? r.as_int32() : static_cast<int32_t>(r.as_double());
  if (rv != 0)
    return Value::int32(lv % rv);
  return Value::float64(NAN);
}

// ─── Field access ───────────────────────────────────────────────────────────

Value RegInterpreter::get_field(Value obj, Atom name) {
  if (obj.is_object()) {
    auto *o = obj.as<Object>();
    if (o)
      return o->get(name);
  }
  return Value::undefined_();
}

void RegInterpreter::put_field(Value obj, Atom name, Value val) {
  if (obj.is_object()) {
    auto *o = obj.as<Object>();
    if (o)
      o->set_own(e_, name, val);
  }
}

// ─── Run bytecode ───────────────────────────────────────────────────────────

Value RegInterpreter::run_bytecode(FunctionBytecode *b, Value *regs, VarRef **upvals, std::vector<VarRef *> *close_list) {
  const auto *ip  = reinterpret_cast<const Instruction *>(b->byte_code_buf.get());
  const auto *end = reinterpret_cast<const Instruction *>(b->byte_code_buf.get() + b->instr_count * 4);

  while (ip < end) {
    Instruction i = *ip;
    RegOp op      = static_cast<RegOp>(i.opcode());
    ip++;

    switch (op) {

      // ── const / move ────────────────────────────────────────────────────────

    case RegOp::NOP:
      break;

    case RegOp::LOADK:
      if (i.bx() < b->cpool_count)
        regs[i.a()] = b->cpool[i.bx()];
      else
        regs[i.a()] = Value::undefined_();
      break;

    case RegOp::LOADINT:
      regs[i.a()] = Value::int32(i.sbx());
      break;

    case RegOp::LOADUNDEF:
      regs[i.a()] = Value::undefined_();
      break;

    case RegOp::LOADNULL:
      regs[i.a()] = Value::null_();
      break;

    case RegOp::LOADTRUE:
      regs[i.a()] = Value::bool_(true);
      break;

    case RegOp::LOADFALSE:
      regs[i.a()] = Value::bool_(false);
      break;

    case RegOp::LOADNIL:
      // R[A]..R[A+B] = undefined
      for (int r = i.a(); r <= i.a() + i.b(); r++)
        regs[r] = Value::undefined_();
      break;

    case RegOp::LOADBOOL:
      regs[i.a()] = Value::bool_(i.b() != 0);
      if (i.c())
        ip++; // skip next instruction
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
    case RegOp::POW: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::float64(std::pow(lv, rv));
    } break;

      // ── bitwise ─────────────────────────────────────────────────────────────

    case RegOp::AND: {
      int32_t l   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r   = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l & r);
      break;
    }
    case RegOp::OR: {
      int32_t l   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r   = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l | r);
      break;
    }
    case RegOp::XOR: {
      int32_t l   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r   = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l ^ r);
      break;
    }
    case RegOp::SHL: {
      int32_t l   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r   = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l << (r & 31));
      break;
    }
    case RegOp::SAR: {
      int32_t l   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r   = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(l >> (r & 31));
      break;
    }
    case RegOp::SHR: {
      int32_t l   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      int32_t r   = regs[i.c()].is_int32() ? regs[i.c()].as_int32() : static_cast<int32_t>(regs[i.c()].as_double());
      regs[i.a()] = Value::int32(static_cast<int32_t>(static_cast<uint32_t>(l) >> (static_cast<uint32_t>(r) & 31)));
      break;
    }

      // ── unary ───────────────────────────────────────────────────────────────

    case RegOp::NEG: {
      double v    = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      regs[i.a()] = Value::float64(-v);
      break;
    }
    case RegOp::BNOT: {
      int32_t v   = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : static_cast<int32_t>(regs[i.b()].as_double());
      regs[i.a()] = Value::int32(~v);
      break;
    }
    case RegOp::LNOT:
      regs[i.a()] = Value::bool_(!is_truthy(regs[i.b()]));
      break;

    case RegOp::INC: {
      Value &v = regs[i.b()];
      if (v.is_int32())
        regs[i.a()] = Value::int32(v.as_int32() + 1);
      else if (v.is_double())
        regs[i.a()] = Value::float64(v.as_double() + 1.0);
      else {
        double dv   = v.as_double();
        regs[i.a()] = Value::float64(dv + 1.0);
      }
      break;
    }
    case RegOp::DEC: {
      Value &v = regs[i.b()];
      if (v.is_int32())
        regs[i.a()] = Value::int32(v.as_int32() - 1);
      else if (v.is_double())
        regs[i.a()] = Value::float64(v.as_double() - 1.0);
      else {
        double dv   = v.as_double();
        regs[i.a()] = Value::float64(dv - 1.0);
      }
      break;
    }

      // ── compare ─────────────────────────────────────────────────────────────

    case RegOp::EQ: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv == rv);
      break;
    }
    case RegOp::NEQ: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv != rv);
      break;
    }
    case RegOp::SEQ: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv == rv);
      break;
    }
    case RegOp::SNEQ: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv != rv);
      break;
    }
    case RegOp::LT: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv < rv);
      break;
    }
    case RegOp::GT: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv > rv);
      break;
    }
    case RegOp::LTE: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
      regs[i.a()] = Value::bool_(lv <= rv);
      break;
    }
    case RegOp::GTE: {
      double lv   = regs[i.b()].is_int32() ? static_cast<double>(regs[i.b()].as_int32()) : regs[i.b()].as_double();
      double rv   = regs[i.c()].is_int32() ? static_cast<double>(regs[i.c()].as_int32()) : regs[i.c()].as_double();
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

    case RegOp::IS_UNDEF:
      if (regs[i.a()].is_undefined())
        ip += i.sbx();
      break;

      // ── object ──────────────────────────────────────────────────────────────

    case RegOp::NEWOBJ: {
      auto obj    = Object::create(e_, Value::undefined_(), Builtin::object);
      regs[i.a()] = obj;
      break;
    }

    case RegOp::GETFIELD: {
      int ci          = i.c();
      Value field_val = (ci < b->cpool_count) ? b->cpool[ci] : Value::undefined_();
      Atom atom       = cpool_to_atom(e_, field_val);
      regs[i.a()]     = get_field(regs[i.b()], atom);
      break;
    }

    case RegOp::SETFIELD: {
      auto ci         = i.b();
      Value field_val = (ci < b->cpool_count) ? b->cpool[ci] : Value::undefined_();
      Atom atom       = cpool_to_atom(e_, field_val);
      put_field(regs[i.a()], atom, regs[i.c()]);
      break;
    }

    case RegOp::DEFINE_FIELD: {
      auto ci         = i.b();
      Value field_val = (ci < b->cpool_count) ? b->cpool[ci] : Value::undefined_();
      Atom atom       = cpool_to_atom(e_, field_val);
      put_field(regs[i.a()], atom, regs[i.c()]);
      break;
    }

    case RegOp::GETELEM: {
      Value &obj = regs[i.b()];
      Value &key = regs[i.c()];
      if (key.is_int32() && obj.is_object()) {
        auto *o = obj.as<Object>();
        if (o && o->clsid == Builtin::array) {
          auto *arr = static_cast<ArrayObject *>(o);
          int idx   = key.as_int32();
          if (idx >= 0 && static_cast<size_t>(idx) < arr->elements.size())
            regs[i.a()] = arr->elements[static_cast<size_t>(idx)];
          else
            regs[i.a()] = Value::undefined_();
          break;
        }
      }
      Atom atom = kAtomNull;
      if (key.is_string())
        atom = e_->intern(key.as<StrPrim>()->view());
      else if (key.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", key.as_int32());
        atom = e_->intern(buf);
      }
      regs[i.a()] = get_field(obj, atom);
      break;
    }

    case RegOp::SETELEM: {
      Value &obj = regs[i.a()];
      Value &key = regs[i.b()];
      Atom atom  = kAtomNull;
      if (key.is_string()) {
        atom = e_->intern(key.as<StrPrim>()->view());
      } else if (key.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", key.as_int32());
        atom = e_->intern(buf);
      }
      put_field(obj, atom, regs[i.c()]);
      break;
    }

    case RegOp::DEFINE_ELEM: {
      Value &obj = regs[i.a()];
      Value &key = regs[i.b()];
      Atom atom  = kAtomNull;
      if (key.is_string()) {
        atom = e_->intern(key.as<StrPrim>()->view());
      } else if (key.is_int32()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", key.as_int32());
        atom = e_->intern(buf);
      }
      put_field(obj, atom, regs[i.c()]);
      if (key.is_int32())
        regs[i.b()] = Value::int32(key.as_int32() + 1);
      break;
    }

      // ── array ───────────────────────────────────────────────────────────────

    case RegOp::NEWARR: {
      auto arr  = ArrayObject::create(e_);
      auto *a   = static_cast<ArrayObject *>(arr.as<Object>());
      int base  = i.b();
      int count = i.c();
      for (int idx = 0; idx < count; idx++)
        a->elements.push_back(regs[base + idx]);
      regs[i.a()] = arr;
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
      Value &v            = regs[i.b()];
      const char *typestr = "object";
      if (v.is_int32() || v.is_double())
        typestr = "number";
      else if (v.is_string())
        typestr = "string";
      else if (v.is_bool())
        typestr = "boolean";
      else if (v.is_undefined())
        typestr = "undefined";
      regs[i.a()] = StrPrim::create(typestr);
      break;
    }

    case RegOp::TOPROPKEY:
      // Placeholder: pass through
      regs[i.a()] = regs[i.b()];
      break;

    case RegOp::SETPROTO:
      if (regs[i.a()].is_object() && regs[i.b()].is_object()) {
        regs[i.a()].as<Object>()->proto = regs[i.b()];
      }
      break;

    case RegOp::TOOBJECT:
      // Placeholder: if already object, pass through
      if (regs[i.b()].is_object())
        regs[i.a()] = regs[i.b()];
      else
        regs[i.a()] = Value::undefined_();
      break;

      // ── call / return ───────────────────────────────────────────────────────

    case RegOp::CALL: {
      int func_reg   = i.b();
      int argc       = i.c();
      Value func_val = regs[func_reg];

      if (func_val.is_object()) {
        auto *obj = func_val.as<Object>();
        if (obj->is_callable()) {
          auto *callable = static_cast<Callable *>(obj);
          if (callable->is_bytecode()) {
            auto *bf = static_cast<BytecodeFunction *>(callable);
            std::vector<VarRef *> call_upvals;
            call_upvals.reserve(bf->var_refs.size());
            for (auto &v : bf->var_refs)
              call_upvals.push_back(v.as<VarRef>());
            Value call_ret = call_bytecode(bf->bytecode, Value::undefined_(), argc, &regs[func_reg + 1], call_upvals.data());
            if (call_ret.is_exception()) {
              if (!catch_stack_.empty() && catch_stack_.back().bytecode == b) {
                auto cf = catch_stack_.back();
                catch_stack_.pop_back();
                regs[cf.exc_reg] = pending_exception_;
                ip               = reinterpret_cast<const Instruction *>(b->byte_code_buf.get() + cf.target_pc * 4);
              } else {
                return Value::exception();
              }
              break;
            }
            regs[i.a()] = call_ret;
          } else {
            regs[i.a()] = callable->call(e_, Value::undefined_(), argc, &regs[func_reg + 1]);
          }
        } else {
          regs[i.a()] = Value::undefined_();
        }
      } else {
        regs[i.a()] = Value::undefined_();
      }
      break;
    }

    case RegOp::CALL_M: {
      int func_reg   = i.b();
      int argc       = i.c();
      Value func_val = regs[func_reg];
      Value this_val = regs[func_reg + 1];

      if (func_val.is_object()) {
        auto *obj = func_val.as<Object>();
        if (obj->is_callable()) {
          regs[i.a()] = static_cast<Callable *>(obj)->call(e_, this_val, argc, &regs[func_reg + 2]);
        } else {
          regs[i.a()] = Value::undefined_();
        }
      } else {
        regs[i.a()] = Value::undefined_();
      }
      break;
    }

    case RegOp::CTOR:
      // Placeholder: same as CALL for now
      regs[i.a()] = Value::undefined_();
      break;

    case RegOp::REGEXP: {
      auto ci = i.bx();
      if (ci + 1 < static_cast<unsigned>(b->cpool_count)) {
        auto *pattern   = b->cpool[ci].as<StrPrim>();
        auto *flags_str = b->cpool[ci + 1].as<StrPrim>();
        if (pattern && flags_str)
          regs[i.a()] = RegExpObj::create(e_, pattern, flags_str);
        else
          regs[i.a()] = Value::undefined_();
      } else {
        regs[i.a()] = Value::undefined_();
      }
      break;
    }

    case RegOp::FCLOSURE: {
      // Load bytecode from cpool
      auto ci = i.bx();
      if (ci < b->cpool_count && b->cpool[ci].is_bytecode()) {
        auto *inner_bc = b->cpool[ci].as<FunctionBytecode>();
        auto closure   = BytecodeFunction::create(e_, inner_bc);
        auto *cl       = closure.as<BytecodeFunction>();

        // Create VarRefs for captured variables from the current frame
        int cv_count = static_cast<int>(inner_bc->closure_var_count);
        cl->var_refs.resize(static_cast<size_t>(cv_count));
        for (int j = 0; j < cv_count; j++) {
          auto &cv = inner_bc->closure_var[j];

          // Find the variable in the enclosing function's vardefs
          int enclosing_var_count = b->arg_count + b->var_count;
          int uv_idx              = -1;
          int reg_idx             = -1;
          for (int k = 0; k < enclosing_var_count; k++) {
            if (b->vardefs[k].var_name == cv.var_name) {
              if (b->vardefs[k].is_captured() && upvals) {
                uv_idx = b->vardefs[k].var_ref_idx;
              } else {
                if (k < b->arg_count) {
                  reg_idx = 1 + k;
                } else {
                  reg_idx = 1 + b->arg_count + (k - b->arg_count);
                }
              }
              break;
            }
          }
          if (uv_idx >= 0 && upvals && upvals[uv_idx]) {
            cl->var_refs[static_cast<size_t>(j)] = Value::var_ref(upvals[uv_idx]);
            upvals[uv_idx]->ref();
          } else if (reg_idx >= 0) {
            Value vr                             = VarRef::create(regs[reg_idx]);
            cl->var_refs[static_cast<size_t>(j)] = vr;
            if (close_list)
              close_list->push_back(vr.as<VarRef>());
          } else {
            Value vr                             = VarRef::create_detached(Value::undefined_());
            cl->var_refs[static_cast<size_t>(j)] = vr;
          }
        }
        regs[i.a()] = closure;
      } else {
        regs[i.a()] = Value::undefined_();
      }
      break;
    }

    case RegOp::RETURN:
      return regs[i.a()];

    case RegOp::RETURN0:
      return Value::undefined_();

    case RegOp::THROW: {
      pending_exception_ = regs[i.a()];
      if (!catch_stack_.empty() && catch_stack_.back().bytecode == b) {
        auto cf = catch_stack_.back();
        catch_stack_.pop_back();
        regs[cf.exc_reg] = pending_exception_;
        ip               = reinterpret_cast<const Instruction *>(b->byte_code_buf.get() + cf.target_pc * 4);
      } else {
        // No catch handler in this frame — propagate to caller
        return Value::exception();
      }
      break;
    }

    case RegOp::CATCH:
      // Record catch frame: exc_reg = A, target = bx (absolute instr index)
      catch_stack_.push_back({static_cast<int>(i.a()), static_cast<int>(i.bx()), b});
      break;

    case RegOp::UNCATCH:
      if (!catch_stack_.empty())
        catch_stack_.pop_back();
      break;

    case RegOp::GOSUB:
      // Push return PC (current ip), jump to target
      return_stack_.push_back(static_cast<int>(ip - reinterpret_cast<const Instruction *>(b->byte_code_buf.get())));
      ip += i.sbx();
      break;

    case RegOp::RET:
      // Pop return PC, jump back
      if (!return_stack_.empty()) {
        int ret_pc = return_stack_.back();
        return_stack_.pop_back();
        ip = reinterpret_cast<const Instruction *>(b->byte_code_buf.get() + ret_pc * 4);
      }
      break;

      // ── upvalue ─────────────────────────────────────────────────────────────

    case RegOp::GETUPVAL:
      if (upvals && upvals[i.b()]) {
        regs[i.a()] = upvals[i.b()]->load();
      } else {
        regs[i.a()] = Value::undefined_();
      }
      break;

    case RegOp::SETUPVAL:
      if (upvals && upvals[i.a()]) {
        upvals[i.a()]->store(regs[i.b()]);
      }
      break;

    case RegOp::CLOSEUPVAL:
      if (upvals && upvals[i.a()]) {
        upvals[i.a()]->close();
      }
      break;

      // ── iteration (for-in) ───────────────────────────────────────────────

    case RegOp::FOR_IN_START: {
      // A=iter_reg, B=obj_reg
      Value &obj = regs[i.b()];
      if (obj.is_object()) {
        auto *o = obj.as<Object>();
        ForInState st;
        st.obj           = o;
        st.shape         = o->shape;
        st.current_index = 0;
        int token        = static_cast<int>(for_in_states_.size());
        for_in_states_.push_back(st);
        regs[i.a()] = Value::int32(token);
      } else {
        regs[i.a()] = Value::int32(-1);
      }
      break;
    }

    case RegOp::FOR_IN_NEXT: {
      // A=key_reg, B=iter_reg, C=more_reg
      int token = regs[i.b()].is_int32() ? regs[i.b()].as_int32() : -1;
      if (token >= 0 && token < static_cast<int>(for_in_states_.size())) {
        auto &st = for_in_states_[static_cast<size_t>(token)];
        if (st.shape && st.current_index < st.shape->size()) {
          Atom atom   = st.shape->entries[st.current_index].atom;
          regs[i.a()] = e_->atom_to_value(atom);
          st.current_index++;
          regs[i.c()] = Value::bool_(true);
        } else {
          regs[i.a()] = Value::undefined_();
          regs[i.c()] = Value::bool_(false);
        }
      } else {
        regs[i.a()] = Value::undefined_();
        regs[i.c()] = Value::bool_(false);
      }
      break;
    }

    default:
      fprintf(stderr, "Unhandled reg opcode: %u\n", static_cast<unsigned>(op));
      return Value::undefined_();
    }
  }

  return Value::undefined_();
}

// ─── Call bytecode ──────────────────────────────────────────────────────────

Value RegInterpreter::call_bytecode(FunctionBytecode *b, Value this_obj, int argc, const Value *argv, VarRef **upvals) {
  uint32_t total_regs = b->reg_count > 0 ? b->reg_count : 256;
  auto regs           = std::make_unique<Value[]>(total_regs);

  // R[0] = this
  regs[0] = this_obj;

  // R[1]..R[argc] = arguments
  for (int i = 0; i < argc && i < b->arg_count; i++)
    regs[1 + i] = argv[i];

  // Unfilled args = undefined
  for (int i = argc; i < b->arg_count; i++)
    regs[1 + i] = Value::undefined_();

  // Local vars initialized to undefined
  for (int i = 0; i < b->var_count; i++)
    regs[1 + b->arg_count + i] = Value::undefined_();

  std::vector<VarRef *> close_list;
  Value result = run_bytecode(b, regs.get(), upvals, &close_list);

  // Close (detach) all VarRefs pointing into this frame
  for (auto *vr : close_list) {
    vr->close();
  }

  return result;
}

// ─── Eval ───────────────────────────────────────────────────────────────────

Value RegInterpreter::eval(FunctionBytecode *b) {
  if (!b)
    return Value::undefined_();
  return call_bytecode(b, e_->global_obj, 0, nullptr, nullptr);
}

Value RegInterpreter::eval_source(const char *source, const char *filename) {
  RegParseState ps(e_);
  ps.init(source, filename);

  if (!ps.compile()) {
    fprintf(stderr, "Compilation failed\n");
    return Value::exception();
  }

  auto *b = lower_reg(ps.cur_func, e_);
  if (!b)
    return Value::exception();

  Value result = eval(b);

  // Cleanup FunctionDef tree
  delete ps.cur_func;

  return result;
}

} // namespace qjsp
