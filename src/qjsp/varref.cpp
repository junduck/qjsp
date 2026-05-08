#include "qjsp/varref.hpp"

namespace qjsp {

Value VarRef::create(Value &slot) {
  auto *vr      = new VarRef();
  vr->ref_count = 1;
  vr->pvalue    = &slot;
  return Value::var_ref(vr);
}

Value VarRef::create_detached(Value v) {
  auto *vr      = new VarRef();
  vr->ref_count = 1;
  vr->value_    = v;
  vr->pvalue    = &vr->value_;
  return Value::var_ref(vr);
}

} // namespace qjsp
