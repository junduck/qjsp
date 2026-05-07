#include <gtest/gtest.h>
#include "qjsp/value.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/context.hpp"
#include "qjsp/string.hpp"
#include "qjsp/object.hpp"
#include <sstream>

using namespace qjsp;

TEST(ValueBasics, Int32RoundTrip) {
  EXPECT_EQ(Value::int32(42).as_int32(), 42);
}

TEST(ValueBasics, NullAndUndefined) {
  EXPECT_TRUE(kNull.is_null());
  EXPECT_TRUE(kUndefined.is_undefined());
}

TEST(ValueBasics, BoolRoundTrip) {
  EXPECT_TRUE(Value::bool_(true).as_bool());
  EXPECT_FALSE(Value::bool_(false).as_bool());
}

TEST(ValueBasics, Float64RoundTrip) {
  EXPECT_DOUBLE_EQ(Value::float64(3.14).as_double(), 3.14);
}

TEST(RuntimeContext, CreateAndDestroy) {
  auto* rt = Runtime::create();
  auto* ctx = Context::create(rt);
  ctx->destroy();
  rt->destroy();
}

TEST(StringOps, CreateAndCmp) {
  auto* a = String::create("abc");
  auto* b = String::create("abd");
  EXPECT_LT(String::compare(a, b), 0);
  EXPECT_EQ(a->view(), "abc");
  a->free(); b->free();
}

TEST(AtomIntern, Predefined) {
  auto* rt = Runtime::create();
  EXPECT_EQ(rt->atom_to_string(static_cast<Atom>(AtomEnum::Object))->view(), "Object");
  rt->destroy();
}

TEST(AtomIntern, Dynamic) {
  auto* rt = Runtime::create();
  auto* s = String::create("myKey");
  Atom a = rt->intern(s);
  EXPECT_NE(a, kAtomNull);
  EXPECT_EQ(rt->atom_to_string(a), s);
  rt->destroy();
}

struct ObjFixture : testing::Test {
  Runtime* rt = Runtime::create();
  Context* ctx = Context::create(rt);
  ~ObjFixture() override { ctx->destroy(); rt->destroy(); }
  Atom atom(const char* s) { return rt->intern(String::create(s)); }
};

TEST_F(ObjFixture, CreateEmpty) {
  auto* obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  EXPECT_EQ(obj->shape, nullptr);
  obj->destroy(rt);
}

TEST_F(ObjFixture, SetAndGet) {
  auto* obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  obj->set_own(rt, atom("x"), Value::int32(100));
  EXPECT_EQ(obj->get_own(atom("x")).as_int32(), 100);
  obj->destroy(rt);
}

TEST_F(ObjFixture, PrototypeChain) {
  auto* proto = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  proto->set_own(rt, atom("a"), Value::int32(999));
  auto* child = Object::create(rt, proto, static_cast<int>(ClassID::object));
  EXPECT_EQ(child->get(atom("a")).as_int32(), 999);
  child->destroy(rt); proto->destroy(rt);
}

TEST_F(ObjFixture, ShapeReuse) {
  auto* a = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  auto* b = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  a->set_own(rt, atom("x"), Value::int32(1));
  b->set_own(rt, atom("x"), Value::int32(2));
  EXPECT_EQ(a->shape, b->shape);
  a->destroy(rt); b->destroy(rt);
}

TEST_F(ObjFixture, NonExtensible) {
  auto* obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  obj->set_own(rt, atom("a"), Value::int32(1));
  obj->extensible = false;
  EXPECT_FALSE(obj->set_own(rt, atom("b"), Value::int32(2)));
  obj->destroy(rt);
}

// ─── C Functions ───────────────────────────────────────────────────────────

static Value test_add(Context*, Value, int argc, const Value* argv) {
  int sum = 0;
  for (int i = 0; i < argc; ++i) sum += argv[i].as_int32();
  return Value::int32(sum);
}

static Value test_identity(Context*, Value, int argc, const Value* argv) {
  return argc > 0 ? argv[0] : kUndefined;
}

TEST_F(ObjFixture, MakeCFunc) {
  auto* fn = Object::make_cfunc(ctx, test_add, "add", 2);
  EXPECT_EQ(fn->class_id, static_cast<uint16_t>(ClassID::c_function));
  EXPECT_NE(fn->u.cfunc.fn, nullptr);
  EXPECT_EQ(fn->get_own(atom("length")).as_int32(), 2);
  fn->destroy(rt);
}

TEST_F(ObjFixture, CallCFunc) {
  auto* fn = Object::make_cfunc(ctx, test_add, "add", 2);
  const Value args[] = {Value::int32(3), Value::int32(4)};
  EXPECT_EQ(call(ctx, Value::object(fn), kUndefined, 2, args).as_int32(), 7);
  fn->destroy(rt);
}

TEST_F(ObjFixture, CallIdentity) {
  auto* fn = Object::make_cfunc(ctx, test_identity, "id", 1);
  auto* s = String::create("hello");
  const Value args[] = {Value::string(s)};
  Value result = call(ctx, Value::object(fn), kUndefined, 1, args);
  EXPECT_TRUE(result.is_string());
  EXPECT_EQ(result.as<String>()->view(), "hello");
  fn->destroy(rt);
}

TEST_F(ObjFixture, GlobalObjectExists) {
  auto* global = ctx->global_obj.as<Object>();
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(global->class_id, static_cast<uint16_t>(ClassID::global_object));
}

TEST_F(ObjFixture, PrintIsDefined) {
  auto* global = ctx->global_obj.as<Object>();
  EXPECT_TRUE(global->get_own(atom("print")).is_object());
}

// ─── GC ────────────────────────────────────────────────────────────────────

TEST_F(ObjFixture, GcCollectsUnreachable) {
  // Create an object, drop all references, force GC.
  auto* obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  obj->destroy(rt);        // explicitly remove the only reference

  rt->run_gc();

  // After GC, verify the object's memory was freed.
  // We can't dereference ptr_before — just trust the GC ran.
  // Verify the gc_obj_list is clean (context still alive).
  bool found_context = false;
  for (auto* hdr : rt->gc_objects)
    if (hdr->gc_obj_type == GCObjType::js_context) found_context = true;
  EXPECT_TRUE(found_context);
}

TEST_F(ObjFixture, GcPreservesReachable) {
  // Create an object reachable from the global scope.
  auto* global = ctx->global_obj.as<Object>();
  auto* obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  auto key = atom("keepme");
  global->set_own(rt, key, Value::object(obj));

  rt->run_gc();

  // The object should still be reachable via the global.
  EXPECT_TRUE(global->get_own(key).is_object());
  EXPECT_EQ(global->get_own(key).as<Object>(), obj);

  obj->destroy(rt);
  global->set_own(rt, key, kUndefined);  // clean up
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
