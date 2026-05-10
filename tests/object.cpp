#include "qjsp/array.hpp"
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
  Value obj = Object::create(rt.get(), Value::undefined_(), ClassID::object);
  EXPECT_EQ(obj.as<Object>()->shape, nullptr);
}

TEST_F(ObjFixture, SetAndGet) {
  Value obj   = Object::create(rt.get(), Value::undefined_(), ClassID::object);
  auto *o     = obj.as<Object>();
  o->set_own(rt.get(), atom("x"), Value::int32(100));
  EXPECT_EQ(o->get_own(atom("x")).as_int32(), 100);
}

TEST_F(ObjFixture, PrototypeChain) {
  Value proto  = Object::create(rt.get(), Value::undefined_(), ClassID::object);
  proto.as<Object>()->set_own(rt.get(), atom("a"), Value::int32(999));
  Value child  = Object::create(rt.get(), proto, ClassID::object);
  EXPECT_EQ(child.as<Object>()->get(atom("a")).as_int32(), 999);
}

TEST_F(ObjFixture, ShapeReuse) {
  Value a = Object::create(rt.get(), Value::undefined_(), ClassID::object);
  Value b = Object::create(rt.get(), Value::undefined_(), ClassID::object);
  a.as<Object>()->set_own(rt.get(), atom("x"), Value::int32(1));
  b.as<Object>()->set_own(rt.get(), atom("x"), Value::int32(2));
  EXPECT_EQ(a.as<Object>()->shape, b.as<Object>()->shape);
}

TEST_F(ObjFixture, NonExtensible) {
  Value obj = Object::create(rt.get(), Value::undefined_(), ClassID::object);
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
  Value fn = CFunctionObj::create(ctx.get(), test_add, "add", 2);
  auto *f  = static_cast<CFunctionObj *>(fn.as<Object>());
  EXPECT_EQ(f->class_id, ClassID::c_function);
  EXPECT_NE(f->fn, nullptr);
  EXPECT_EQ(f->get_own(atom("length")).as_int32(), 2);
}

TEST_F(ObjFixture, CallCFunc) {
  Value fn          = CFunctionObj::create(ctx.get(), test_add, "add", 2);
  const Value args[] = {Value::int32(3), Value::int32(4)};
  EXPECT_EQ(call(ctx.get(), fn, Value::undefined_(), 2, args).as_int32(), 7);
}

TEST_F(ObjFixture, CallIdentity) {
  Value fn          = CFunctionObj::create(ctx.get(), test_identity, "id", 1);
  Value s           = String::create("hello");
  const Value args[] = {s};
  Value result      = call(ctx.get(), fn, Value::undefined_(), 1, args);
  EXPECT_TRUE(result.is_string());
  EXPECT_EQ(result.as<String>()->view(), "hello");
}

TEST_F(ObjFixture, GlobalObjectExists) {
  auto *global = ctx->global_obj.as<Object>();
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(global->class_id, ClassID::global_object);
}

TEST_F(ObjFixture, PrintIsDefined) {
  auto *global = ctx->global_obj.as<Object>();
  EXPECT_TRUE(global->get_own(atom("print")).is_object());
}

TEST_F(ObjFixture, GcCollectsUnreachable) {
  {
    Value obj = Object::create(rt.get(), Value::undefined_(), ClassID::object);
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
  Value obj    = Object::create(rt.get(), Value::undefined_(), ClassID::object);
  auto key     = atom("keepme");
  auto *objptr = obj.as<Object>();
  global->set_own(rt.get(), key, obj);
  rt->run_gc();
  EXPECT_TRUE(global->get_own(key).is_object());
  EXPECT_EQ(global->get_own(key).as<Object>(), objptr);
  global->set_own(rt.get(), key, Value::undefined_());
}

TEST_F(ObjFixture, ArrayIteratorManual) {
  // Create array [10, 20] and test iterator directly
  auto arr = ArrayObject::create(rt.get(), ctx->array_proto);
  auto *a  = static_cast<ArrayObject *>(arr.as<Object>());
  a->elements.push_back(Value::int32(10));
  a->elements.push_back(Value::int32(20));

  // Get Symbol.iterator method from array
  auto si_atom = static_cast<Atom>(AtomEnum::Symbol_iterator);
  Value si_fn  = arr.as<Object>()->get(si_atom);
  EXPECT_TRUE(si_fn.is_object());
  EXPECT_TRUE(si_fn.as<Object>()->is_callable());

  // Call Symbol.iterator() → iterator object
  auto *callable = static_cast<Callable *>(si_fn.as<Object>());
  Value iter_val = callable->call(ctx.get(), arr, 0, nullptr);
  EXPECT_TRUE(iter_val.is_object());

  // Call iterator.next() → {value: 10, done: false}
  auto *iter = iter_val.as<Object>();
  Value next_fn = iter->get(rt->intern("next"));
  EXPECT_TRUE(next_fn.is_object());

  auto *next_callable = static_cast<Callable *>(next_fn.as<Object>());
  Value r1 = next_callable->call(ctx.get(), iter_val, 0, nullptr);
  EXPECT_TRUE(r1.is_object());
  EXPECT_EQ(r1.as<Object>()->get_own(rt->intern("value")).as_int32(), 10);
  EXPECT_FALSE(r1.as<Object>()->get_own(rt->intern("done")).as_bool());

  // Call iterator.next() again → {value: 20, done: false}
  Value r2 = next_callable->call(ctx.get(), iter_val, 0, nullptr);
  EXPECT_EQ(r2.as<Object>()->get_own(rt->intern("value")).as_int32(), 20);

  // Call iterator.next() → {done: true}
  Value r3 = next_callable->call(ctx.get(), iter_val, 0, nullptr);
  EXPECT_TRUE(r3.as<Object>()->get_own(rt->intern("done")).as_bool());
}
