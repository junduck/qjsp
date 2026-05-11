#include "qjsp/regexp.hpp"
#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include <re2/re2.h>

namespace qjsp {

Value RegExpObj::create(Context *ctx, StrPrim *pattern, StrPrim *flags_str) {
  re2::StringPiece pat(pattern->data, pattern->len());
  re2::RE2::Options opts;

  uint8_t flags = 0;
  std::string_view fs(flags_str->data, flags_str->len());
  for (char c : fs) {
    switch (c) {
    case 'g':
      flags |= 1;
      break;
    case 'i':
      flags |= 2;
      opts.set_case_sensitive(false);
      break;
    case 'm':
      flags |= 4;
      opts.set_one_line(false);
      break;
    case 's':
      flags |= 8;
      opts.set_dot_nl(true);
      break;
    case 'u':
      flags |= 16;
      break;
    case 'y':
      flags |= 32;
      break;
    }
  }

  auto compiled = std::make_unique<re2::RE2>(pat, opts);
  if (!compiled->ok())
    return Value::exception();

  auto *obj        = new RegExpObj();
  obj->ref_count   = 1;
  obj->gc_obj_type = GCObjType::js_object;
  obj->class_id    = ClassID::regexp;
  obj->proto       = ctx->regexp_proto;
  obj->regex       = std::move(compiled);
  obj->flags       = flags;
  ctx->rt->add_gc_object(obj);
  return Value::object(obj);
}

// ── Prototype initialisation ────────────────────────────────────────────

static Value regexp_test(Context *ctx, Value this_val, int argc, const Value *argv) {
  auto *obj = this_val.as<RegExpObj>();
  if (!obj || argc < 1)
    return Value::bool_(false);

  auto *str = argv[0].as<StrPrim>();
  if (!str)
    return Value::bool_(false);

  re2::StringPiece input(str->data, str->len());
  bool matched = obj->regex->Match(input, 0, str->len(), re2::RE2::UNANCHORED, nullptr, 0);
  return Value::bool_(matched);
}

void init_regexp_prototype(Context *ctx) {
  auto proto   = Object::create(ctx->rt, Value::undefined_(), ClassID::regexp);
  auto test_fn = CFunctionObj::create(ctx, regexp_test, "test", 1);
  proto.as<Object>()->set_own(ctx->rt, ctx->rt->intern("test"), test_fn);
  ctx->regexp_proto = proto;
}

} // namespace qjsp
