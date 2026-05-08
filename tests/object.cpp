#include "qjsp/context.hpp"
#include "qjsp/object.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

struct ObjFixture : testing::Test {
  std::unique_ptr<Runtime> rt  = std::make_unique<Runtime>();
  std::unique_ptr<Context> ctx = std::make_unique<Context>(rt.get());
  Atom atom(const char *s) { return rt->intern(s); }
};

TEST_F(ObjFixture, CreateEmpty) {
  Value obj = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  EXPECT_EQ(obj.as<Object>()->shape, nullptr);
}

TEST_F(ObjFixture, SetAndGet) {
  Value obj   = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  auto *o     = obj.as<Object>();
  o->set_own(rt.get(), atom("x"), Value::int32(100));
  EXPECT_EQ(o->get_own(atom("x")).as_int32(), 100);
}

TEST_F(ObjFixture, PrototypeChain) {
  Value proto  = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  proto.as<Object>()->set_own(rt.get(), atom("a"), Value::int32(999));
  Value child  = Object::create(rt.get(), proto, static_cast<int>(ClassID::object));
  EXPECT_EQ(child.as<Object>()->get(atom("a")).as_int32(), 999);
}

TEST_F(ObjFixture, ShapeReuse) {
  Value a = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  Value b = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  a.as<Object>()->set_own(rt.get(), atom("x"), Value::int32(1));
  b.as<Object>()->set_own(rt.get(), atom("x"), Value::int32(2));
  EXPECT_EQ(a.as<Object>()->shape, b.as<Object>()->shape);
}

TEST_F(ObjFixture, NonExtensible) {
  Value obj = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  auto *o   = obj.as<Object>();
  o->set_own(rt.get(), atom("a"), Value::int32(1));
  o->extensible = false;
  EXPECT_FALSE(o->set_own(rt.get(), atom("b"), Value::int32(2)));
}

static Value test_add(Context *, Value, int argc, const Value *argv) {
  int sum = 0;
  for (int i = 0; i < argc; ++i)
    sum += argv[i].as_int32();
  return Value::int32(sum);
}

static Value test_identity(Context *, Value, int argc, const Value *argv) { return argc > 0 ? argv[0] : Value::undefined_(); }

TEST_F(ObjFixture, MakeCFunc) {
  Value fn = Object::make_cfunc(ctx.get(), test_add, "add", 2);
  auto *f  = fn.as<Object>();
  EXPECT_EQ(f->class_id, static_cast<uint16_t>(ClassID::c_function));
  EXPECT_NE(f->u.cfunc.fn, nullptr);
  EXPECT_EQ(f->get_own(atom("length")).as_int32(), 2);
}

TEST_F(ObjFixture, CallCFunc) {
  Value fn          = Object::make_cfunc(ctx.get(), test_add, "add", 2);
  const Value args[] = {Value::int32(3), Value::int32(4)};
  EXPECT_EQ(call(ctx.get(), fn, Value::undefined_(), 2, args).as_int32(), 7);
}

TEST_F(ObjFixture, CallIdentity) {
  Value fn          = Object::make_cfunc(ctx.get(), test_identity, "id", 1);
  Value s           = String::create("hello");
  const Value args[] = {s};
  Value result      = call(ctx.get(), fn, Value::undefined_(), 1, args);
  EXPECT_TRUE(result.is_string());
  EXPECT_EQ(result.as<String>()->view(), "hello");
}

TEST_F(ObjFixture, GlobalObjectExists) {
  auto *global = ctx->global_obj.as<Object>();
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(global->class_id, static_cast<uint16_t>(ClassID::global_object));
}

TEST_F(ObjFixture, PrintIsDefined) {
  auto *global = ctx->global_obj.as<Object>();
  EXPECT_TRUE(global->get_own(atom("print")).is_object());
}

TEST_F(ObjFixture, GcCollectsUnreachable) {
  {
    Value obj = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  }
  rt->run_gc();
  bool found_context = false;
  for (auto *hdr : rt->gc_objects)
    if (hdr->gc_obj_type == GCObjType::js_context)
      found_context = true;
  EXPECT_TRUE(found_context);
}

TEST_F(ObjFixture, GcPreservesReachable) {
  auto *global = ctx->global_obj.as<Object>();
  Value obj    = Object::create(rt.get(), Value::undefined_(), static_cast<int>(ClassID::object));
  auto key     = atom("keepme");
  auto *objptr = obj.as<Object>();
  global->set_own(rt.get(), key, obj);
  rt->run_gc();
  EXPECT_TRUE(global->get_own(key).is_object());
  EXPECT_EQ(global->get_own(key).as<Object>(), objptr);
  global->set_own(rt.get(), key, Value::undefined_());
}
