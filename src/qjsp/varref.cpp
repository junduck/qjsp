#include "qjsp/varref.hpp"

namespace qjsp {

VarRef *VarRef::create(Value *slot) {
  auto *vr = new VarRef();
  vr->ref_count = 1;
  vr->pvalue = slot;
  vr->is_detached_ = false;
  return vr;
}

VarRef *VarRef::create_detached(Value v) {
  auto *vr = new VarRef();
  vr->ref_count = 1;
  vr->value_ = v;
  vr->pvalue = &vr->value_;
  vr->is_detached_ = true;
  return vr;
}

void VarRef::close() {
  if (!is_detached_) {
    value_ = *pvalue;
    pvalue = &value_;
    is_detached_ = true;
  }
}

} // namespace qjsp
