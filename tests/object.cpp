#include "qjsp/engine.hpp"
#include "qjsp/object.hpp"
#include "qjsp/array.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

struct ObjFixture : testing::Test {
  std::unique_ptr<Engine> e = std::make_unique<Engine>();
  Atom atom(const char *s) { return e->intern(s); }
};

TEST_F(ObjFixture, CreateEmpty) {
  Value obj = Object::create(e.get(), Value::undefined_(), Builtin::object);
  EXPECT_EQ(obj.as<Object>()->shape, nullptr);
}

TEST_F(ObjFixture, SetAndGet) {
  Value obj = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *o   = obj.as<Object>();
  o->set_own(e.get(), atom("x"), Value::int32(100));
  EXPECT_EQ(o->get_own(atom("x")).as_int32(), 100);
}

TEST_F(ObjFixture, PrototypeChain) {
  Value proto = Object::create(e.get(), Value::undefined_(), Builtin::object);
  proto.as<Object>()->set_own(e.get(), atom("a"), Value::int32(999));
  Value child = Object::create(e.get(), proto, Builtin::object);
  EXPECT_EQ(child.as<Object>()->get(atom("a")).as_int32(), 999);
}

TEST_F(ObjFixture, ShapeReuse) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  a.as<Object>()->set_own(e.get(), atom("x"), Value::int32(1));
  b.as<Object>()->set_own(e.get(), atom("x"), Value::int32(2));
  EXPECT_EQ(a.as<Object>()->shape, b.as<Object>()->shape);
}

TEST_F(ObjFixture, NonExtensible) {
  Value obj = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *o   = obj.as<Object>();
  o->set_own(e.get(), atom("a"), Value::int32(1));
  o->extensible = false;
  EXPECT_FALSE(o->set_own(e.get(), atom("b"), Value::int32(2)));
}

static Value test_add(Engine *, Value, int argc, const Value *argv) {
  int sum = 0;
  for (int i = 0; i < argc; ++i)
    sum += argv[i].as_int32();
  return Value::int32(sum);
}

static Value test_identity(Engine *, Value, int argc, const Value *argv) { return argc > 0 ? argv[0] : Value::undefined_(); }

TEST_F(ObjFixture, MakeCFunc) {
  Value fn = CFunctionObj::create(e.get(), test_add, "add", 2);
  auto *f  = static_cast<CFunctionObj *>(fn.as<Object>());
  EXPECT_EQ(f->clsid, Builtin::object);
  EXPECT_NE(f->fn, nullptr);
  EXPECT_EQ(f->get_own(atom("length")).as_int32(), 2);
}

TEST_F(ObjFixture, CallCFunc) {
  Value fn           = CFunctionObj::create(e.get(), test_add, "add", 2);
  const Value args[] = {Value::int32(3), Value::int32(4)};
  EXPECT_EQ(call(e.get(), fn, Value::undefined_(), 2, args).as_int32(), 7);
}

TEST_F(ObjFixture, CallIdentity) {
  Value fn           = CFunctionObj::create(e.get(), test_identity, "id", 1);
  Value s            = StrPrim::create("hello");
  const Value args[] = {s};
  Value result       = call(e.get(), fn, Value::undefined_(), 1, args);
  EXPECT_TRUE(result.is_string());
  EXPECT_EQ(result.as<StrPrim>()->view(), "hello");
}

TEST_F(ObjFixture, GlobalObjectExists) {
  auto *global = e->global_obj.as<Object>();
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(global->clsid, Builtin::object);
}

TEST_F(ObjFixture, PrintIsDefined) {
  auto *global = e->global_obj.as<Object>();
  EXPECT_TRUE(global->get_own(atom("print")).is_object());
}

TEST_F(ObjFixture, GcCollectsUnreachable) {
  {
    Value obj = Object::create(e.get(), Value::undefined_(), Builtin::object);
  }
  e->run_gc();
  bool found_global = false;
  for (auto *hdr : e->gc_objects)
    if (hdr->gc_obj_type == GCObjType::js_object)
      found_global = true;
  EXPECT_TRUE(found_global);
}

TEST_F(ObjFixture, GcPreservesReachable) {
  auto *global = e->global_obj.as<Object>();
  Value obj    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto key     = atom("keepme");
  auto *objptr = obj.as<Object>();
  global->set_own(e.get(), key, obj);
  e->run_gc();
  EXPECT_TRUE(global->get_own(key).is_object());
  EXPECT_EQ(global->get_own(key).as<Object>(), objptr);
  global->set_own(e.get(), key, Value::undefined_());
}

TEST_F(ObjFixture, ArrayIteratorManual) {
  auto arr = ArrayObject::create(e.get());
  auto *a  = static_cast<ArrayObject *>(arr.as<Object>());
  a->elements.push_back(Value::int32(10));
  a->elements.push_back(Value::int32(20));

  auto si_atom = e->known[WellKnown::symbol_iterator];
  Value si_fn  = arr.as<Object>()->get(si_atom);
  EXPECT_TRUE(si_fn.is_object());
  EXPECT_TRUE(si_fn.as<Object>()->is_callable());

  auto *callable = static_cast<Callable *>(si_fn.as<Object>());
  Value iter_val = callable->call(e.get(), arr, 0, nullptr);
  EXPECT_TRUE(iter_val.is_object());

  auto *iter    = iter_val.as<Object>();
  Value next_fn = iter->get(e->intern("next"));
  EXPECT_TRUE(next_fn.is_object());

  auto *next_callable = static_cast<Callable *>(next_fn.as<Object>());
  Value r1            = next_callable->call(e.get(), iter_val, 0, nullptr);
  EXPECT_TRUE(r1.is_object());
  EXPECT_EQ(r1.as<Object>()->get_own(e->intern("value")).as_int32(), 10);
  EXPECT_FALSE(r1.as<Object>()->get_own(e->intern("done")).as_bool());

  Value r2 = next_callable->call(e.get(), iter_val, 0, nullptr);
  EXPECT_EQ(r2.as<Object>()->get_own(e->intern("value")).as_int32(), 20);

  Value r3 = next_callable->call(e.get(), iter_val, 0, nullptr);
  EXPECT_TRUE(r3.as<Object>()->get_own(e->intern("done")).as_bool());
}
