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

// ── GC stress tests (kGcThresholdInit=8 in debug) ────────────────────────

TEST_F(ObjFixture, GcStackValueSurvives) {
  Value obj = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *ptr = obj.as<Object>();
  for (int i = 0; i < 20; i++) {
    Value tmp = Object::create(e.get(), Value::undefined_(), Builtin::object);
    tmp.as<Object>()->set_own(e.get(), atom("i"), Value::int32(i));
  }
  EXPECT_EQ(obj.as<Object>(), ptr);
  EXPECT_TRUE(obj.is_object());
}

TEST_F(ObjFixture, GcCycleCollected) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  aptr->set_own(e.get(), atom("next"), b);
  bptr->set_own(e.get(), atom("next"), a);
  size_t before = e->gc_objects.size();
  a = Value::undefined_();
  b = Value::undefined_();
  e->run_gc();
  bool found_a = false, found_b = false;
  for (auto *hdr : e->gc_objects) {
    if (hdr == aptr) found_a = true;
    if (hdr == bptr) found_b = true;
  }
  EXPECT_FALSE(found_a);
  EXPECT_FALSE(found_b);
}

TEST_F(ObjFixture, GcCycleWithExternalRefPreserved) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  a.as<Object>()->set_own(e.get(), atom("next"), b);
  b.as<Object>()->set_own(e.get(), atom("next"), a);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  e->run_gc();
  EXPECT_TRUE(a.is_object());
  EXPECT_TRUE(b.is_object());
  EXPECT_EQ(a.as<Object>(), aptr);
  EXPECT_EQ(b.as<Object>(), bptr);
}

TEST_F(ObjFixture, GcTransitiveRevival) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c = Object::create(e.get(), Value::undefined_(), Builtin::object);
  a.as<Object>()->set_own(e.get(), atom("ref"), b);
  b.as<Object>()->set_own(e.get(), atom("ref"), c);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  b = Value::undefined_();
  c = Value::undefined_();
  e->run_gc();
  EXPECT_EQ(a.as<Object>(), aptr);
  EXPECT_EQ(a.as<Object>()->get_own(atom("ref")).as<Object>(), bptr);
  EXPECT_EQ(a.as<Object>()->get_own(atom("ref")).as<Object>()->get_own(atom("ref")).as<Object>(), cptr);
}

TEST_F(ObjFixture, GcDeepCycle) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value d = Object::create(e.get(), Value::undefined_(), Builtin::object);
  a.as<Object>()->set_own(e.get(), atom("next"), b);
  b.as<Object>()->set_own(e.get(), atom("next"), c);
  c.as<Object>()->set_own(e.get(), atom("next"), d);
  d.as<Object>()->set_own(e.get(), atom("next"), a);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  auto *dptr = d.as<Object>();
  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  d = Value::undefined_();
  e->run_gc();
  bool found = false;
  for (auto *hdr : e->gc_objects) {
    if (hdr == aptr || hdr == bptr || hdr == cptr || hdr == dptr)
      found = true;
  }
  EXPECT_FALSE(found);
}

TEST_F(ObjFixture, GcDeepCyclePartialExternal) {
  Value nodes[4];
  for (int i = 0; i < 4; i++)
    nodes[i] = Object::create(e.get(), Value::undefined_(), Builtin::object);
  for (int i = 0; i < 4; i++)
    nodes[i].as<Object>()->set_own(e.get(), atom("next"), nodes[(i + 1) % 4]);
  Object *ptrs[4];
  for (int i = 0; i < 4; i++) ptrs[i] = nodes[i].as<Object>();
  nodes[1] = Value::undefined_();
  nodes[2] = Value::undefined_();
  nodes[3] = Value::undefined_();
  e->run_gc();
  for (int i = 0; i < 4; i++) {
    bool found = false;
    for (auto *hdr : e->gc_objects)
      if (hdr == ptrs[i]) found = true;
    EXPECT_TRUE(found) << "node " << i << " should survive (reachable from node 0)";
  }
}

TEST_F(ObjFixture, GcArrayElementsSurvive) {
  auto arr = ArrayObject::create(e.get());
  auto *a  = static_cast<ArrayObject *>(arr.as<Object>());
  for (int i = 0; i < 20; i++) {
    Value inner = Object::create(e.get(), Value::undefined_(), Builtin::object);
    inner.as<Object>()->set_own(e.get(), atom("val"), Value::int32(i));
    a->elements.push_back(inner);
  }
  e->run_gc();
  for (int i = 0; i < 20; i++) {
    EXPECT_TRUE(a->elements[i].is_object());
    EXPECT_EQ(a->elements[i].as<Object>()->get_own(atom("val")).as_int32(), i);
  }
}

TEST_F(ObjFixture, GcProtoChainSurvives) {
  Value p1 = Object::create(e.get(), Value::null_(), Builtin::object);
  Value p2 = Object::create(e.get(), p1, Builtin::object);
  Value p3 = Object::create(e.get(), p2, Builtin::object);
  p1.as<Object>()->set_own(e.get(), atom("root"), Value::int32(42));
  auto *p1ptr = p1.as<Object>();
  auto *p2ptr = p2.as<Object>();
  p1 = Value::undefined_();
  p2 = Value::undefined_();
  e->run_gc();
  EXPECT_TRUE(p3.is_object());
  EXPECT_EQ(p3.as<Object>()->get(atom("root")).as_int32(), 42);
  EXPECT_EQ(p3.as<Object>()->proto.as<Object>(), p2ptr);
  EXPECT_EQ(p3.as<Object>()->proto.as<Object>()->proto.as<Object>(), p1ptr);
}
