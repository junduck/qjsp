#include "qjsp/value.hpp"
#include "qjsp/bytecode.hpp"
#include "qjsp/varref.hpp"

namespace qjsp {

Value::~Value() {
  if (is_pointer() && !is_null_ptr()) {
    auto *rc = get_ref_counted();
    rc->unref();
    if (rc->ref_count == 0 && tag_prefix() < kTagObject) {
      if (is_string()) {
        ::operator delete(as_pointer());
      } else if (is_var_ref()) {
        delete static_cast<VarRef *>(as_pointer());
      } else if (is_bytecode()) {
        delete static_cast<FunctionBytecode *>(as_pointer());
      }
    }
  }
}

} // namespace qjsp
