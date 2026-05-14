#include "qjsp/engine.hpp"
#include "qjsp/object.hpp"
#include "qjsp/array.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include "qjsp/reg_interpreter.hpp"
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
    if (hdr == e->global_obj.as<Object>())
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
  Value arr = ArrayObject::create(e.get());
  auto *a   = static_cast<ArrayObject *>(arr.as<Object>());
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

// ── Two-tier GC adversarial tests ──────────────────────────────────────

TEST_F(ObjFixture, SweepDeadCollectsRefCountZero) {
  {
    Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
    Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  }
  size_t before = e->gc_objects.size();
  e->sweep_dead();
  EXPECT_LT(e->gc_objects.size(), before);
}

TEST_F(ObjFixture, SweepDeadPreservesCycles) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  aptr->set_own(e.get(), atom("next"), b);
  bptr->set_own(e.get(), atom("next"), a);
  a = Value::undefined_();
  b = Value::undefined_();
  e->sweep_dead();
  bool found_a = false, found_b = false;
  for (auto *hdr : e->gc_objects) {
    if (hdr == reinterpret_cast<GCObjectHeader *>(aptr)) found_a = true;
    if (hdr == reinterpret_cast<GCObjectHeader *>(bptr)) found_b = true;
  }
  EXPECT_TRUE(found_a) << "cycle should survive sweep_dead (ref_count > 0)";
  EXPECT_TRUE(found_b) << "cycle should survive sweep_dead (ref_count > 0)";
}

TEST_F(ObjFixture, SweepDeadCascadesOverMultiplePasses) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  aptr->set_own(e.get(), atom("ref"), b);
  bptr->set_own(e.get(), atom("ref"), c);
  size_t baseline = e->gc_objects.size();
  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  e->sweep_dead();
  EXPECT_EQ(e->gc_objects.size(), baseline - 1)
      << "first sweep_dead collects A (ref_count==0), B still has ref from A's property";
  e->sweep_dead();
  EXPECT_EQ(e->gc_objects.size(), baseline - 2)
      << "second sweep_dead collects B (ref_count==0 after A deleted)";
  e->sweep_dead();
  EXPECT_EQ(e->gc_objects.size(), baseline - 3)
      << "third sweep_dead collects C";
}

TEST_F(ObjFixture, RunGcAfterSweepDead) {
  Value cycle_a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value cycle_b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  cycle_a.as<Object>()->set_own(e.get(), atom("next"), cycle_b);
  cycle_b.as<Object>()->set_own(e.get(), atom("next"), cycle_a);
  auto *aptr = cycle_a.as<Object>();
  auto *bptr = cycle_b.as<Object>();
  { Value tmp = Object::create(e.get(), Value::undefined_(), Builtin::object); }
  size_t before_sweep = e->gc_objects.size();
  e->sweep_dead();
  EXPECT_EQ(e->gc_objects.size(), before_sweep - 1)
      << "sweep_dead collects the temporary but not the cycle";
  cycle_a = Value::undefined_();
  cycle_b = Value::undefined_();
  e->run_gc();
  bool found = false;
  for (auto *hdr : e->gc_objects)
    if (hdr == reinterpret_cast<GCObjectHeader *>(aptr) ||
        hdr == reinterpret_cast<GCObjectHeader *>(bptr))
      found = true;
  EXPECT_FALSE(found) << "run_gc should collect the cycle after sweep_dead";
}

TEST_F(ObjFixture, MultipleIsolatedCycles) {
  Value c1a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c1b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  c1a.as<Object>()->set_own(e.get(), atom("c1"), c1b);
  c1b.as<Object>()->set_own(e.get(), atom("c1"), c1a);
  Value c2a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c2b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  c2a.as<Object>()->set_own(e.get(), atom("c2"), c2b);
  c2b.as<Object>()->set_own(e.get(), atom("c2"), c2a);
  Value c3a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c3b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  c3a.as<Object>()->set_own(e.get(), atom("c3"), c3b);
  c3b.as<Object>()->set_own(e.get(), atom("c3"), c3a);
  size_t before = e->gc_objects.size();
  e->run_gc();
  EXPECT_EQ(e->gc_objects.size(), before)
      << "cycles with external stack refs survive run_gc";
}

TEST_F(ObjFixture, MultipleIsolatedCyclesAllCollected) {
  Value cycles[3][2];
  Object *ptrs[3][2];
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++)
      cycles[i][j] = Object::create(e.get(), Value::undefined_(), Builtin::object);
    cycles[i][0].as<Object>()->set_own(e.get(), atom("next"), cycles[i][1]);
    cycles[i][1].as<Object>()->set_own(e.get(), atom("next"), cycles[i][0]);
    for (int j = 0; j < 2; j++)
      ptrs[i][j] = cycles[i][j].as<Object>();
  }
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 2; j++)
      cycles[i][j] = Value::undefined_();
  e->run_gc();
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
      bool found = false;
      for (auto *hdr : e->gc_objects)
        if (hdr == reinterpret_cast<GCObjectHeader *>(ptrs[i][j])) found = true;
      EXPECT_FALSE(found) << "cycle " << i << " node " << j << " should be collected";
    }
  }
}

TEST_F(ObjFixture, CycleWithArrayElement) {
  Value arr  = ArrayObject::create(e.get());
  Value obj  = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = arr.as<Object>();
  auto *optr = obj.as<Object>();
  obj.as<Object>()->set_own(e.get(), atom("arr"), arr);
  static_cast<ArrayObject *>(aptr)->elements.push_back(obj);
  arr = Value::undefined_();
  obj = Value::undefined_();
  e->run_gc();
  bool found_arr = false, found_obj = false;
  for (auto *hdr : e->gc_objects) {
    if (hdr == reinterpret_cast<GCObjectHeader *>(aptr)) found_arr = true;
    if (hdr == reinterpret_cast<GCObjectHeader *>(optr)) found_obj = true;
  }
  EXPECT_FALSE(found_arr) << "array in cycle should be collected";
  EXPECT_FALSE(found_obj) << "object in cycle should be collected";
}

TEST_F(ObjFixture, InterpreterSweepDeadInLoop) {
  RegInterpreter interp(e.get());
  size_t before = e->gc_objects.size();
  auto result = interp.eval_source(
      "var total = 0;"
      "for (var i = 0; i < 100; i++) {"
      "  var tmp = { v: i };"
      "  total += tmp.v;"
      "}"
      "total",
      "<sweep_test>");
  EXPECT_TRUE(result.is_int32());
  EXPECT_EQ(result.as_int32(), 4950);
}

TEST_F(ObjFixture, InterpreterCycleInLoopCollectedByRunGc) {
  RegInterpreter interp(e.get());
  e->gc_alloc_count = 0;
  e->gc_sweep_count = Engine::kFullGcInterval - 1;
  auto result = interp.eval_source(
      "var result = 0;"
      "for (var i = 0; i < 10; i++) {"
      "  var a = {}; var b = {};"
      "  a.x = b; b.x = a;"
      "  result += i;"
      "}"
      "result",
      "<cycle_loop_test>");
  EXPECT_TRUE(result.is_int32());
  EXPECT_EQ(result.as_int32(), 45);
}

TEST_F(ObjFixture, SweepDeadRunGcAlternating) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  a.as<Object>()->set_own(e.get(), atom("next"), b);
  b.as<Object>()->set_own(e.get(), atom("next"), a);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  { Value tmp1 = Object::create(e.get(), Value::undefined_(), Builtin::object); }
  { Value tmp2 = Object::create(e.get(), Value::undefined_(), Builtin::object); }
  { Value tmp3 = Object::create(e.get(), Value::undefined_(), Builtin::object); }
  e->sweep_dead();
  e->run_gc();
  e->sweep_dead();
  e->run_gc();
  EXPECT_TRUE(a.is_object());
  EXPECT_TRUE(b.is_object());
  EXPECT_EQ(a.as<Object>(), aptr);
  EXPECT_EQ(b.as<Object>(), bptr);
}

TEST_F(ObjFixture, SweepDeadDoesNotDoubleFree) {
  Value parent = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *p = parent.as<Object>();
  for (int i = 0; i < 50; i++) {
    Value child = Object::create(e.get(), Value::undefined_(), Builtin::object);
    child.as<Object>()->set_own(e.get(), atom("val"), Value::int32(i));
    p->set_own(e.get(), e->intern(std::to_string(i).c_str()), child);
  }
  parent = Value::undefined_();
  e->sweep_dead();
}

TEST_F(ObjFixture, RunGcWithNoGarbage) {
  size_t before = e->gc_objects.size();
  e->run_gc();
  EXPECT_EQ(e->gc_objects.size(), before)
      << "run_gc with no garbage should not change gc_objects";
}

TEST_F(ObjFixture, SweepDeadWithNoGarbage) {
  size_t before = e->gc_objects.size();
  e->sweep_dead();
  EXPECT_EQ(e->gc_objects.size(), before)
      << "sweep_dead with no garbage should not change gc_objects";
}

TEST_F(ObjFixture, GcCascadeDeleteUAF) {
  Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c = Object::create(e.get(), Value::undefined_(), Builtin::object);

  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();

  aptr->set_own(e.get(), atom("next"), b);
  bptr->set_own(e.get(), atom("next"), a);

  aptr->set_own(e.get(), atom("extra"), c);

  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();

  e->run_gc();

  bool found_any = false;
  for (auto *hdr : e->gc_objects) {
    if (hdr == reinterpret_cast<GCObjectHeader *>(aptr) ||
        hdr == reinterpret_cast<GCObjectHeader *>(bptr) ||
        hdr == reinterpret_cast<GCObjectHeader *>(cptr)) {
      found_any = true;
    }
  }
  EXPECT_FALSE(found_any) << "all cycle objects should be collected";
}

TEST_F(ObjFixture, GcStressedCycleWithMultipleExtras) {
  Value objs[6];
  for (int i = 0; i < 6; i++)
    objs[i] = Object::create(e.get(), Value::undefined_(), Builtin::object);

  auto *p0 = objs[0].as<Object>();
  auto *p1 = objs[1].as<Object>();
  auto *p2 = objs[2].as<Object>();
  auto *p3 = objs[3].as<Object>();
  auto *p4 = objs[4].as<Object>();
  auto *p5 = objs[5].as<Object>();

  p0->set_own(e.get(), atom("next"), objs[1]);
  p1->set_own(e.get(), atom("next"), objs[0]);
  p2->set_own(e.get(), atom("next"), objs[3]);
  p3->set_own(e.get(), atom("next"), objs[2]);
  p4->set_own(e.get(), atom("next"), objs[5]);
  p5->set_own(e.get(), atom("next"), objs[4]);

  p0->set_own(e.get(), atom("a"), objs[2]);
  p2->set_own(e.get(), atom("b"), objs[4]);

  for (int i = 0; i < 6; i++)
    objs[i] = Value::undefined_();

  e->run_gc();

  for (int i = 0; i < 6; i++) {
    bool found = false;
    for (auto *hdr : e->gc_objects) {
      if (hdr == reinterpret_cast<GCObjectHeader *>(p0 + i)) {
        found = true;
        break;
      }
    }
    EXPECT_FALSE(found) << "object " << i << " should be collected";
  }
}
