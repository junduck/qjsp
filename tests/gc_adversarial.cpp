#include "qjsp/engine.hpp"
#include "qjsp/object.hpp"
#include "qjsp/array.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

struct GcAdvFixture : testing::Test {
  std::unique_ptr<Engine> e = std::make_unique<Engine>();
  Atom atom(const char *s) { return e->intern(s); }

  bool is_alive(Object *ptr) {
    for (auto *hdr : e->gc_objects)
      if (hdr == reinterpret_cast<GCObjectHeader *>(ptr))
        return true;
    return false;
  }

  bool is_alive(void *ptr) { return is_alive(static_cast<Object *>(ptr)); }
};

// ── self-referencing cycles ──────────────────────────────────────────────

TEST_F(GcAdvFixture, SelfReferencePropertyCycle) {
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  aptr->set_own(e.get(), atom("self"), a); // a.self = a → ref_count = 2 (stack + property)
  a = Value::undefined_();                 // ref_count = 1 (property only)
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr)) << "self-cycle should be collected";
}

TEST_F(GcAdvFixture, SelfReferenceProtoCycle) {
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  aptr->proto = a; // a.__proto__ = a → ref_count = 2
  a           = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr)) << "self-proto-cycle should be collected";
}

TEST_F(GcAdvFixture, ArraySelfReferenceCycle) {
  Value arr    = ArrayObject::create(e.get());
  auto *aptr   = static_cast<ArrayObject *>(arr.as<Object>());
  aptr->set_elem(0, arr); // arr[0] = arr → ref_count = 2
  arr = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr)) << "array self-cycle should be collected";
}

// ── proto cycles ─────────────────────────────────────────────────────────

TEST_F(GcAdvFixture, ProtoCycle2Node) {
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  aptr->proto = b;
  bptr->proto = a;
  a = Value::undefined_();
  b = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr)) << "proto cycle: a should be collected";
  EXPECT_FALSE(is_alive(bptr)) << "proto cycle: b should be collected";
}

TEST_F(GcAdvFixture, ProtoCycle3Node) {
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  aptr->proto = b;
  bptr->proto = c;
  cptr->proto = a;
  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(bptr));
  EXPECT_FALSE(is_alive(cptr));
}

// ── multiple edges to the same child ─────────────────────────────────────

TEST_F(GcAdvFixture, MultiplePropertiesToOneChild) {
  Value parent = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value child  = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *pptr   = parent.as<Object>();
  auto *cptr   = child.as<Object>();
  // 5 properties all pointing to the same child
  pptr->set_own(e.get(), atom("a"), child);
  pptr->set_own(e.get(), atom("b"), child);
  pptr->set_own(e.get(), atom("c"), child);
  pptr->set_own(e.get(), atom("d"), child);
  pptr->set_own(e.get(), atom("e"), child);
  // child.ref_count = 5 (from parent's 5 props) + stack = 6
  child = Value::undefined_(); // child.ref_count = 5
  // parent still holds a ref via stack → parent survives, child survives transitively
  e->run_gc();
  EXPECT_TRUE(is_alive(pptr)) << "parent with external ref survives";
  EXPECT_TRUE(is_alive(cptr))
      << "child survives because parent (external ref) transitively marks it";
  // Now drop parent
  parent = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(pptr));
  EXPECT_FALSE(is_alive(cptr)) << "child should be collected with parent";
}

TEST_F(GcAdvFixture, PropertyAndProtoToSameChild) {
  Value parent = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value child  = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *pptr   = parent.as<Object>();
  auto *cptr   = child.as<Object>();
  pptr->proto = child; // one edge through proto
  pptr->set_own(e.get(), atom("x"), child); // another edge through property
  // child.ref_count = 2 (proto + property) + 1 (stack) = 3
  child = Value::undefined_(); // child.ref_count = 2
  parent = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(pptr));
  EXPECT_FALSE(is_alive(cptr))
      << "child with two internal edges from parent should be collected";
}

// ── dead object as proto of another dead object ──────────────────────────

TEST_F(GcAdvFixture, DeadProtoChainCollected) {
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  aptr->proto = b; // A.proto = B
  bptr->proto = c; // B.proto = C
  // External refs: a, b, c on stack → all survive
  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(bptr));
  EXPECT_FALSE(is_alive(cptr));
}

// ── complex mixed topology ───────────────────────────────────────────────

TEST_F(GcAdvFixture, MixedArrayObjectCycle) {
  Value arr    = ArrayObject::create(e.get());
  Value obj1   = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value obj2   = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr   = static_cast<ArrayObject *>(arr.as<Object>());
  auto *o1ptr  = obj1.as<Object>();
  auto *o2ptr  = obj2.as<Object>();

  aptr->set_elem(0, obj1);   // arr[0] = obj1
  o1ptr->set_own(e.get(), atom("next"), obj2); // obj1.next = obj2
  o2ptr->set_own(e.get(), atom("back"), arr);   // obj2.back = arr

  arr  = Value::undefined_();
  obj1 = Value::undefined_();
  obj2 = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr)) << "array in mixed cycle";
  EXPECT_FALSE(is_alive(o1ptr)) << "obj1 in mixed cycle";
  EXPECT_FALSE(is_alive(o2ptr)) << "obj2 in mixed cycle";
}

TEST_F(GcAdvFixture, MixedArrayObjectCycleWithExternalRef) {
  Value arr   = ArrayObject::create(e.get());
  Value obj1  = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value obj2  = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr  = static_cast<ArrayObject *>(arr.as<Object>());
  auto *o1ptr = obj1.as<Object>();
  auto *o2ptr = obj2.as<Object>();

  aptr->set_elem(0, obj1);
  o1ptr->set_own(e.get(), atom("next"), obj2);
  o2ptr->set_own(e.get(), atom("back"), arr);

  // drop arr and obj1, but keep obj2 on the stack → the whole cycle survives
  arr  = Value::undefined_();
  obj1 = Value::undefined_();
  e->run_gc();
  EXPECT_TRUE(is_alive(aptr)) << "array survives via cycle reachable from obj2";
  EXPECT_TRUE(is_alive(o1ptr));
  EXPECT_TRUE(is_alive(o2ptr));
}

TEST_F(GcAdvFixture, ObjectAsElementAndProto) {
  Value arr   = ArrayObject::create(e.get());
  Value obj   = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr  = static_cast<ArrayObject *>(arr.as<Object>());
  auto *optr  = obj.as<Object>();

  aptr->set_elem(0, obj); // arr[0] = obj
  optr->proto = arr;      // obj.__proto__ = arr (cycle!)

  arr = Value::undefined_();
  obj = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(optr));
}

// ── lollipop: chain leading into cycle ───────────────────────────────────

TEST_F(GcAdvFixture, LollipopTopologyCollected) {
  // A → B → C ↔ D (C and D form a cycle, B links in, A is the entry)
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value d    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  auto *dptr = d.as<Object>();

  aptr->set_own(e.get(), atom("next"), b);
  bptr->set_own(e.get(), atom("next"), c);
  cptr->set_own(e.get(), atom("next"), d);
  dptr->set_own(e.get(), atom("next"), c); // cycle C↔D

  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  d = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(bptr));
  EXPECT_FALSE(is_alive(cptr));
  EXPECT_FALSE(is_alive(dptr));
}

TEST_F(GcAdvFixture, LollipopWithExternalRefAtCycle) {
  // A → B → C → D → A  (single 4-node ring)
  // Keep C on stack, drop A, B, D → all 4 survive because C is in the ring.
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value d    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();
  auto *dptr = d.as<Object>();

  aptr->set_own(e.get(), atom("next"), b);
  bptr->set_own(e.get(), atom("next"), c);
  cptr->set_own(e.get(), atom("next"), d);
  dptr->set_own(e.get(), atom("next"), a); // ring: A→B→C→D→A

  a = Value::undefined_();
  b = Value::undefined_();
  d = Value::undefined_();
  e->run_gc();
  EXPECT_TRUE(is_alive(aptr));
  EXPECT_TRUE(is_alive(bptr));
  EXPECT_TRUE(is_alive(cptr));
  EXPECT_TRUE(is_alive(dptr));
}

// ── overlapping cycles ───────────────────────────────────────────────────

TEST_F(GcAdvFixture, OverlappingCyclesAllCollected) {
  // A ↔ B ↔ C ↔ A (3 nodes all referencing each other)
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();

  // fully connected triangle: each node has refs to the other two
  aptr->set_own(e.get(), atom("b"), b);
  aptr->set_own(e.get(), atom("c"), c);
  bptr->set_own(e.get(), atom("a"), a);
  bptr->set_own(e.get(), atom("c"), c);
  cptr->set_own(e.get(), atom("a"), a);
  cptr->set_own(e.get(), atom("b"), b);

  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(bptr));
  EXPECT_FALSE(is_alive(cptr));
}

// ── count-based precision checks ─────────────────────────────────────────

TEST_F(GcAdvFixture, CountPrecisionAllCollected) {
  size_t before = e->gc_objects.size();
  {
    Value objs[10];
    for (int i = 0; i < 10; i++)
      objs[i] = Object::create(e.get(), Value::undefined_(), Builtin::object);
    for (int i = 0; i < 9; i++)
      objs[i].as<Object>()->set_own(e.get(), atom("next"), objs[i + 1]);
    objs[9].as<Object>()->set_own(e.get(), atom("next"), objs[0]); // close the ring
  }
  e->run_gc();
  EXPECT_EQ(e->gc_objects.size(), before)
      << "all 10 objects in the ring should be collected, leaving no trace";
}

TEST_F(GcAdvFixture, CountPrecisionPartialExternal) {
  size_t before = e->gc_objects.size();
  Value keep;
  {
    Value objs[10];
    for (int i = 0; i < 10; i++)
      objs[i] = Object::create(e.get(), Value::undefined_(), Builtin::object);
    for (int i = 0; i < 9; i++)
      objs[i].as<Object>()->set_own(e.get(), atom("next"), objs[i + 1]);
    objs[9].as<Object>()->set_own(e.get(), atom("next"), objs[0]);
    keep = objs[3]; // hold node 3 externally
  }
  e->run_gc();
  // All 10 survive because one node (index 3) has an external ref
  EXPECT_EQ(e->gc_objects.size(), before + 10)
      << "all 10 objects in the ring should survive (node 3 has external ref)";
}

// ── sweep_dead interaction with cycles ───────────────────────────────────

TEST_F(GcAdvFixture, SweepDeadLeavesCyclesThenRunGcCollects) {
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  aptr->set_own(e.get(), atom("next"), b);
  bptr->set_own(e.get(), atom("next"), a);
  a = Value::undefined_();
  b = Value::undefined_();

  e->sweep_dead();
  // sweep_dead must NOT collect the cycle (ref_count > 0 for both)
  EXPECT_TRUE(is_alive(aptr)) << "sweep_dead should leave cyclic objects alone";
  EXPECT_TRUE(is_alive(bptr));

  e->run_gc(); // now break the cycle
  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(bptr));
}

// ── allocation during gc_clear_refs phase (defensive) ────────────────────

TEST_F(GcAdvFixture, AllocDuringRunGcDoesNotCorrupt) {
  // Create a cycle then trigger run_gc. The GC should complete cleanly
  // regardless of what's in gc_objects at the start.
  {
    Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
    Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
    a.as<Object>()->set_own(e.get(), atom("next"), b);
    b.as<Object>()->set_own(e.get(), atom("next"), a);
  }
  // Allocate more objects before GC (not in a cycle, should be swept by sweep_dead or survive)
  Value survivor = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *sptr     = survivor.as<Object>();

  e->run_gc(); // the cycle should be collected, survivor should live

  EXPECT_TRUE(is_alive(sptr)) << "externally-referenced object survives run_gc";
}

// ── array with many elements forming sub-cycles ──────────────────────────

TEST_F(GcAdvFixture, ArrayElementsSubCycleCollected) {
  Value arr  = ArrayObject::create(e.get());
  auto *aptr = static_cast<ArrayObject *>(arr.as<Object>());
  {
    Value a = Object::create(e.get(), Value::undefined_(), Builtin::object);
    Value b = Object::create(e.get(), Value::undefined_(), Builtin::object);
    auto *optr_a = a.as<Object>();
    auto *optr_b = b.as<Object>();
    optr_a->set_own(e.get(), atom("next"), b);
    optr_b->set_own(e.get(), atom("next"), a);
    aptr->set_elem(0, a);
    aptr->set_elem(1, b);
  }
  // arr is on stack → arr survives, elements survive transitively
  e->run_gc();
  EXPECT_TRUE(is_alive(aptr));
  EXPECT_EQ(aptr->elements.size(), 2u);
  EXPECT_TRUE(aptr->elements[0].is_object());
  EXPECT_TRUE(aptr->elements[1].is_object());

  // Now drop arr
  arr = Value::undefined_();
  e->run_gc();
  EXPECT_FALSE(is_alive(aptr)) << "array with sub-cycle elements should be collected";
}

// ── gc_clear_refs ordering: dead object is proto of another dead ─────────

TEST_F(GcAdvFixture, DeadProtoOfDeadObject) {
  // A has property "x" pointing to B, B.proto = C, all dead.
  // Tests that gc_clear_refs doesn't trip over proto chains within the dead set.
  Value a    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value b    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  Value c    = Object::create(e.get(), Value::undefined_(), Builtin::object);
  auto *aptr = a.as<Object>();
  auto *bptr = b.as<Object>();
  auto *cptr = c.as<Object>();

  aptr->set_own(e.get(), atom("ref"), b);
  bptr->proto = c;

  a = Value::undefined_();
  b = Value::undefined_();
  c = Value::undefined_();
  e->run_gc();

  EXPECT_FALSE(is_alive(aptr));
  EXPECT_FALSE(is_alive(bptr));
  EXPECT_FALSE(is_alive(cptr));
}

// ── empty gc_objects list ────────────────────────────────────────────────

TEST_F(GcAdvFixture, SweepDeadEmptyList) {
  // After collecting everything except builtins, sweep_dead should be safe
  {
    Value tmp = Object::create(e.get(), Value::undefined_(), Builtin::object);
  }
  e->sweep_dead();
  size_t after_sweep = e->gc_objects.size();
  e->sweep_dead();
  EXPECT_EQ(e->gc_objects.size(), after_sweep)
      << "sweep_dead on an already-cleaned list is idempotent";
}

TEST_F(GcAdvFixture, RunGcEmptyList) {
  size_t before = e->gc_objects.size();
  e->run_gc();
  EXPECT_EQ(e->gc_objects.size(), before)
      << "run_gc without new allocations is idempotent";
}
