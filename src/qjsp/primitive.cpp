#include "qjsp/primitive.hpp"
#include "qjsp/engine.hpp"
#include "qjsp/function.hpp"

namespace qjsp {
// ─── instanceof ───────────────────────────────────────────────────────────────

bool instanceof(Engine *e, Value obj, Value ctor) {
  if (!obj.is_object() || !ctor.is_object() || !ctor.is_callable())
    return false;

  auto *ctor_obj = ctor.as<Object>();

  // Symbol.hasInstance override — coerce result via ToBoolean
  Value hasInst = ctor_obj->get(e, e->known[WellKnown::symbol_hasInstance]);
  if (hasInst.is_callable()) {
    const Value args[] = {obj};
    Value ret          = static_cast<Callable *>(hasInst.as<Object>())->call(e, ctor, 1, args);
    return ret.is_truthy();
  }

  // proto chain walk
  Value ctor_proto = ctor_obj->get_own(e, e->intern("prototype"));
  if (!ctor_proto.is_object())
    return false;
  auto *target = ctor_proto.as<Object>();
  for (auto *cur = obj.as<Object>(); cur; cur = cur->proto.is_object() ? cur->proto.as<Object>() : nullptr)
    if (cur == target)
      return true;
  return false;
}
} // namespace qjsp
