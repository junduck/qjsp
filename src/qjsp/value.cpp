#include "qjsp/value.hpp"
#include "qjsp/bigint.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/varref.hpp"

namespace qjsp {

Value::~Value() {
  if (!is_pointer() || is_nullptr())
    return;
  auto *rc = get_ref_counted();
  rc->unref();
  if (rc->ref_count != 0 || tag_prefix() == kTagObject)
    return;
  switch (rc_type()) {
  case RCType::StrPrim:
    ::operator delete(as_pointer());
    break;
  case RCType::VarRef:
    delete static_cast<VarRef *>(as_pointer());
    break;
  case RCType::Bytecode:
    delete static_cast<Bytecode *>(as_pointer());
    break;
  case RCType::BigInt:
    delete static_cast<Bigint *>(as_pointer());
    break;
  }
}

} // namespace qjsp
