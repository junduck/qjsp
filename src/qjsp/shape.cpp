#include "qjsp/shape.hpp"

namespace qjsp {

int Shape::find(Atom atom) const {
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].atom == atom)
      return static_cast<int>(i);
  return -1;
}

} // namespace qjsp
