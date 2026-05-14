#pragma once

#include "value.hpp"

namespace qjsp {

struct Engine;

bool instanceof(Engine *e, Value obj, Value ctor);

} // namespace qjsp
