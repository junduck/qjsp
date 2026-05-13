#include "qjsp/bigint.hpp"

namespace qjsp {
Bigint *Bigint::allocate_raw(int64_t val) {
  auto *i      = new Bigint{};
  i->data      = val;
  i->ref_count = 1;
  return i;
}
} // namespace qjsp
