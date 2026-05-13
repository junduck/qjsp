#include "qjsp/string.hpp"
#include <cstdlib>

namespace qjsp {

StrPrim *StrPrim::allocate_raw(std::string_view src) {
  auto *s = static_cast<StrPrim *>(::operator new(sizeof(StrPrim) + src.size() + 1));
  auto *p = reinterpret_cast<char *>(s + 1);
  std::copy(src.begin(), src.end(), p);
  p[src.size()] = 0;

  new (s) StrPrim();
  s->ref_count = 1;
  s->meta      = static_cast<uint32_t>(src.size()) & 0x7FFFFFFFu;
  s->data      = p;
  return s;
}

int StrPrim::compare(const StrPrim *a, const StrPrim *b) {
  if (a == b)
    return 0;
  auto min_len = std::min(a->len(), b->len());
  int cmp      = std::memcmp(a->data, b->data, min_len);
  if (cmp != 0)
    return cmp;
  return static_cast<int>(a->len()) - static_cast<int>(b->len());
}

} // namespace qjsp
