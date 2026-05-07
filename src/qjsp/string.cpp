#include "qjsp/string.hpp"
#include <cstdlib>

namespace qjsp {

String *String::create(std::string_view src) {
  auto *s = static_cast<String *>(operator new(sizeof(String) + src.size() + 1));
  auto *p = reinterpret_cast<char *>(s + 1);
  std::memcpy(p, src.data(), src.size());
  p[src.size()] = 0;

  new (s) String();
  s->ref_count = 1;
  s->set_len(static_cast<uint32_t>(src.size()));
  s->data = p;
  return s;
}

int String::compare(const String *a, const String *b) {
  if (a == b)
    return 0;
  auto min_len = std::min(a->len(), b->len());
  int cmp = std::memcmp(a->data, b->data, min_len);
  if (cmp != 0)
    return cmp;
  return static_cast<int>(a->len()) - static_cast<int>(b->len());
}

} // namespace qjsp
