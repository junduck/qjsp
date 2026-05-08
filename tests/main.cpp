#include "qjsp/context.hpp"
#include "qjsp/lexer.hpp"
#include "qjsp/object.hpp"
#include "qjsp/reg_interpreter.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace qjsp;

TEST(ValueBasics, Int32RoundTrip) { EXPECT_EQ(Value::int32(42).as_int32(), 42); }

TEST(ValueBasics, NullAndUndefined) {
  EXPECT_TRUE(Value::null_().is_null());
  EXPECT_TRUE(Value::undefined_().is_undefined());
}

TEST(ValueBasics, BoolRoundTrip) {
  EXPECT_TRUE(Value::bool_(true).as_bool());
  EXPECT_FALSE(Value::bool_(false).as_bool());
}

TEST(ValueBasics, Float64RoundTrip) { EXPECT_DOUBLE_EQ(Value::float64(3.14).as_double(), 3.14); }

TEST(RuntimeContext, CreateAndDestroy) {
  auto *rt  = Runtime::create();
  auto *ctx = Context::create(rt);
  ctx->destroy();
  rt->destroy();
}

TEST(StringOps, CreateAndCmp) {
  auto *a = String::create("abc");
  auto *b = String::create("abd");
  EXPECT_LT(String::compare(a, b), 0);
  EXPECT_EQ(a->view(), "abc");
  a->unref();
  b->unref();
}

TEST(AtomIntern, Predefined) {
  auto *rt = Runtime::create();
  EXPECT_EQ(rt->atom_to_string(static_cast<Atom>(AtomEnum::Object))->view(), "Object");
  rt->destroy();
}

TEST(AtomIntern, Dynamic) {
  auto *rt = Runtime::create();
  auto *s  = String::create("myKey");
  Atom a   = rt->intern(s);
  EXPECT_NE(a, kAtomNull);
  EXPECT_EQ(rt->atom_to_string(a), s);
  rt->destroy();
}

struct ObjFixture : testing::Test {
  Runtime *rt  = Runtime::create();
  Context *ctx = Context::create(rt);
  ~ObjFixture() override {
    ctx->destroy();
    rt->destroy();
  }
  Atom atom(const char *s) { return rt->intern(String::create(s)); }
};

TEST_F(ObjFixture, CreateEmpty) {
  auto *obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  EXPECT_EQ(obj->shape, nullptr);
  obj->destroy(rt);
}

TEST_F(ObjFixture, SetAndGet) {
  auto *obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  obj->set_own(rt, atom("x"), Value::int32(100));
  EXPECT_EQ(obj->get_own(atom("x")).as_int32(), 100);
  obj->destroy(rt);
}

TEST_F(ObjFixture, PrototypeChain) {
  auto *proto = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  proto->set_own(rt, atom("a"), Value::int32(999));
  auto *child = Object::create(rt, proto, static_cast<int>(ClassID::object));
  EXPECT_EQ(child->get(atom("a")).as_int32(), 999);
  child->destroy(rt);
  proto->destroy(rt);
}

TEST_F(ObjFixture, ShapeReuse) {
  auto *a = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  auto *b = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  a->set_own(rt, atom("x"), Value::int32(1));
  b->set_own(rt, atom("x"), Value::int32(2));
  EXPECT_EQ(a->shape, b->shape);
  a->destroy(rt);
  b->destroy(rt);
}

TEST_F(ObjFixture, NonExtensible) {
  auto *obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  obj->set_own(rt, atom("a"), Value::int32(1));
  obj->extensible = false;
  EXPECT_FALSE(obj->set_own(rt, atom("b"), Value::int32(2)));
  obj->destroy(rt);
}

static Value test_add(Context *, Value, int argc, const Value *argv) {
  int sum = 0;
  for (int i = 0; i < argc; ++i)
    sum += argv[i].as_int32();
  return Value::int32(sum);
}

static Value test_identity(Context *, Value, int argc, const Value *argv) { return argc > 0 ? argv[0] : Value::undefined_(); }

TEST_F(ObjFixture, MakeCFunc) {
  auto *fn = Object::make_cfunc(ctx, test_add, "add", 2);
  EXPECT_EQ(fn->class_id, static_cast<uint16_t>(ClassID::c_function));
  EXPECT_NE(fn->u.cfunc.fn, nullptr);
  EXPECT_EQ(fn->get_own(atom("length")).as_int32(), 2);
  fn->destroy(rt);
}

TEST_F(ObjFixture, CallCFunc) {
  auto *fn           = Object::make_cfunc(ctx, test_add, "add", 2);
  const Value args[] = {Value::int32(3), Value::int32(4)};
  EXPECT_EQ(call(ctx, Value::object(fn), Value::undefined_(), 2, args).as_int32(), 7);
  fn->destroy(rt);
}

TEST_F(ObjFixture, CallIdentity) {
  auto *fn           = Object::make_cfunc(ctx, test_identity, "id", 1);
  auto *s            = String::create("hello");
  const Value args[] = {Value::string(s)};
  Value result       = call(ctx, Value::object(fn), Value::undefined_(), 1, args);
  EXPECT_TRUE(result.is_string());
  EXPECT_EQ(result.as<String>()->view(), "hello");
  fn->destroy(rt);
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
  auto *obj = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  obj->destroy(rt);
  rt->run_gc();
  bool found_context = false;
  for (auto *hdr : rt->gc_objects)
    if (hdr->gc_obj_type == GCObjType::js_context)
      found_context = true;
  EXPECT_TRUE(found_context);
}

TEST_F(ObjFixture, GcPreservesReachable) {
  auto *global = ctx->global_obj.as<Object>();
  auto *obj    = Object::create(rt, nullptr, static_cast<int>(ClassID::object));
  auto key     = atom("keepme");
  global->set_own(rt, key, Value::object(obj));
  rt->run_gc();
  EXPECT_TRUE(global->get_own(key).is_object());
  EXPECT_EQ(global->get_own(key).as<Object>(), obj);
  global->set_own(rt, key, Value::undefined_());
  obj->destroy(rt);
}

// ─── Lexer ──────────────────────────────────────────────────────────────────

struct LexerFixture : testing::Test {
  Runtime *rt = Runtime::create();
  Lexer lexer;

  void init_lexer(const char *source) { lexer.init(rt, "test.js", reinterpret_cast<const uint8_t *>(source), std::strlen(source)); }

  ~LexerFixture() override { rt->destroy(); }
};

TEST_F(LexerFixture, Eof) {
  init_lexer("");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_EOF);
}

TEST_F(LexerFixture, SingleCharTokens) {
  init_lexer("(){}[],;:");
  for (int expected : {'(', ')', '{', '}', '[', ']', ',', ';', ':'}) {
    EXPECT_TRUE(lexer.next_token());
    EXPECT_EQ(lexer.token.type, expected);
  }
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_EOF);
}

TEST_F(LexerFixture, Identifiers) {
  init_lexer("foo bar _x $y");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_to_string(lexer.token.u.ident.atom)->view(), "foo");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_to_string(lexer.token.u.ident.atom)->view(), "bar");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_to_string(lexer.token.u.ident.atom)->view(), "_x");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_to_string(lexer.token.u.ident.atom)->view(), "$y");
}

TEST_F(LexerFixture, Keywords) {
  init_lexer("if else return var function");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IF);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_ELSE);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_RETURN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_VAR);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_FUNCTION);
}

TEST_F(LexerFixture, StringLiterals) {
  init_lexer("\"hello\" 'world'");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRING);
  EXPECT_STREQ(lexer.token.u.str.str, "hello");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRING);
  EXPECT_STREQ(lexer.token.u.str.str, "world");
}

TEST_F(LexerFixture, StringEscapes) {
  init_lexer("\"a\\nb\\tc\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRING);
  EXPECT_STREQ(lexer.token.u.str.str, "a\nb\tc");
}

TEST_F(LexerFixture, Numbers) {
  init_lexer("42 3.14 0xFF");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NUMBER);
  EXPECT_DOUBLE_EQ(lexer.token.u.num.val, 42.0);

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NUMBER);
  EXPECT_DOUBLE_EQ(lexer.token.u.num.val, 3.14);

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NUMBER);
  EXPECT_DOUBLE_EQ(lexer.token.u.num.val, 255.0);
}

TEST_F(LexerFixture, Operators) {
  init_lexer("+ - * / % == === != !== < > <= >= && || ?? ?.");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '+');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '-');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '*');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '/');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '%');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_EQ);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRICT_EQ);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NEQ);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRICT_NEQ);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '<');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '>');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_LTE);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_GTE);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_LAND);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_LOR);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_DOUBLE_QUESTION_MARK);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_QUESTION_MARK_DOT);
}

TEST_F(LexerFixture, ArrowAndEllipsis) {
  init_lexer("=> ...");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_ARROW);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_ELLIPSIS);
}

TEST_F(LexerFixture, AssignmentOperators) {
  init_lexer("= += -= *= /= %= <<= >>= >>>= &= |= ^= &&= ||= ??= **= **");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, '=');
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_PLUS_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_MINUS_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_MUL_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_DIV_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_MOD_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_SHL_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_SAR_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_SHR_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_AND_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_OR_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_XOR_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_LAND_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_LOR_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_DOUBLE_QUESTION_MARK_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_POW_ASSIGN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_POW);
}

TEST_F(LexerFixture, IncrementDecrement) {
  init_lexer("++ --");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_INC);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_DEC);
}

TEST_F(LexerFixture, TemplateLiteral) {
  init_lexer("`hello`");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_TEMPLATE);
  EXPECT_STREQ(lexer.token.u.str.str, "hello");
  EXPECT_EQ(lexer.token.u.str.sep, '`');
}

TEST_F(LexerFixture, CommentSkip) {
  init_lexer("a /* block */ b // line\na c");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
}

TEST_F(LexerFixture, PeekToken) {
  init_lexer("in of import export function");
  EXPECT_EQ(lexer.peek_token(false), TOK_IN);
  EXPECT_EQ(lexer.peek_token(true), TOK_IN);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IN);
  EXPECT_EQ(lexer.peek_token(false), TOK_OF);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(lexer.peek_token(false), TOK_IMPORT);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IMPORT);
  EXPECT_EQ(lexer.peek_token(false), TOK_EXPORT);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_EXPORT);
  EXPECT_EQ(lexer.peek_token(false), TOK_FUNCTION);
}

TEST_F(LexerFixture, LookaheadArrow) {
  init_lexer("=>");
  EXPECT_EQ(lexer.peek_token(true), TOK_ARROW);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_ARROW);
}

// ─── Reg Parser ─────────────────────────────────────────────────────────────

struct RegParserFixture : testing::Test {
  Runtime *rt  = Runtime::create();
  Context *ctx = Context::create(rt);
  RegParseState ps{rt, ctx};

  bool compile(const char *source) {
    ps.init(source, "test.js");
    return ps.compile();
  }

  ~RegParserFixture() override {
    ctx->destroy();
    rt->destroy();
  }
};

TEST_F(RegParserFixture, Empty) { EXPECT_TRUE(compile("")); }

TEST_F(RegParserFixture, ExpressionStatement) { EXPECT_TRUE(compile("42;")); }

TEST_F(RegParserFixture, VarDecl) { EXPECT_TRUE(compile("var x = 1;")); }

TEST_F(RegParserFixture, LetDecl) { EXPECT_TRUE(compile("let y = 2;")); }

TEST_F(RegParserFixture, ConstDecl) { EXPECT_TRUE(compile("const z = 3;")); }

TEST_F(RegParserFixture, IfStatement) { EXPECT_TRUE(compile("if (true) { 42; }")); }

TEST_F(RegParserFixture, IfElseStatement) { EXPECT_TRUE(compile("if (false) { 1; } else { 2; }")); }

TEST_F(RegParserFixture, ReturnStatement) { EXPECT_TRUE(compile("function f() { return 42; }")); }

TEST_F(RegParserFixture, WhileLoop) { EXPECT_TRUE(compile("while (true) { break; }")); }

TEST_F(RegParserFixture, ForLoop) { EXPECT_TRUE(compile("for (var i = 0; i < 10; i = i + 1) { }")); }

TEST_F(RegParserFixture, ForLoopSimple) { EXPECT_TRUE(compile("for (;;) { }")); }

TEST_F(RegParserFixture, ForLoopNoUpdate) { EXPECT_TRUE(compile("for (var i = 0; i < 10;) { i = i + 1; }")); }

TEST_F(RegParserFixture, FunctionDeclaration) { EXPECT_TRUE(compile("function add(a, b) { return a + b; }")); }

TEST_F(RegParserFixture, MultipleStatements) { EXPECT_TRUE(compile("var a = 1; var b = 2; var c = a + b;")); }

TEST_F(RegParserFixture, NestedBlocks) { EXPECT_TRUE(compile("{ var x = 1; { var y = 2; } }")); }

TEST_F(RegParserFixture, ThrowStatement) { EXPECT_TRUE(compile("throw 42;")); }

// ─── Reg Interpreter ────────────────────────────────────────────────────────

struct RegInterpFixture : testing::Test {
  Runtime *rt  = Runtime::create();
  Context *ctx = Context::create(rt);
  RegInterpreter interp{ctx};

  Value eval(const char *source) { return interp.eval_source(source); }

  ~RegInterpFixture() override {
    ctx->destroy();
    rt->destroy();
  }
};

TEST_F(RegInterpFixture, LiteralInt) {
  Value v = eval("42;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 42);
}

TEST_F(RegInterpFixture, Arithmetic) {
  Value v = eval("1 + 2 * 3;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 7);
}

TEST_F(RegInterpFixture, VarDeclAndUse) {
  Value v = eval("var x = 10; x + 5;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 15);
}

TEST_F(RegInterpFixture, IfStatement) {
  Value v = eval("var x = 0; if (1) { x = 42; } x;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 42);
}

TEST_F(RegInterpFixture, FunctionCall) {
  Value v = eval("function add(a, b) { return a + b; } add(3, 4);");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 7);
}

TEST_F(RegInterpFixture, BuiltinPrint) {
  Value v = eval("print;");
  EXPECT_TRUE(v.is_object());
}

// ─── Phase D: Objects + Arrays ──────────────────────────────────────────────

TEST_F(RegInterpFixture, ObjectLiteralEmpty) {
  Value v = eval("var x = {}; x;");
  EXPECT_TRUE(v.is_object());
}

TEST_F(RegInterpFixture, ObjectLiteralSimple) {
  Value v = eval("var x = {a: 1, b: 2}; x.a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 1);
}

TEST_F(RegInterpFixture, ObjectLiteralStringKey) {
  Value v = eval("var x = {'key': 42}; x.key;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 42);
}

TEST_F(RegInterpFixture, ArrayLiteralEmpty) {
  Value v = eval("var x = []; x;");
  EXPECT_TRUE(v.is_object());
}

TEST_F(RegInterpFixture, ArrayLiteralSimple) {
  Value v = eval("var x = [1, 2, 3]; x;");
  EXPECT_TRUE(v.is_object());
}

TEST_F(RegInterpFixture, SetFieldAccess) {
  Value v = eval("var x = {}; x.a = 10; x.a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 10);
}

TEST_F(RegInterpFixture, ComplexFieldChain) {
  Value v = eval("var a = {b: {c: 7}}; a.b.c;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 7);
}

// ─── Phase C: Closures ──────────────────────────────────────────────────────

TEST_F(RegInterpFixture, ClosureRead) {
  Value v = eval("function outer() { var x = 1; return function() { return x; }; } var c = outer(); c();");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 1);
}

TEST_F(RegInterpFixture, ClosureNested) {
  Value v = eval("function outer() { var x = 3; function inner() { return x; } return inner(); } outer();");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

// ─── Try/Catch ──────────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, TryCatchBasic) {
  Value v = eval("var a = 0; try { a = 1; } catch(e) { a = 2; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 1);
}

TEST_F(RegInterpFixture, TryCatchCaught) {
  Value v = eval("var a = 0; try { throw 99; } catch(e) { a = e; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 99);
}

// ─── Switch ─────────────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, SwitchBasic) {
  Value v = eval("var a = 0; switch(2) { case 1: a=10; break; case 2: a=20; break; default: a=30; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 20);
}

TEST_F(RegInterpFixture, SwitchDefault) {
  Value v = eval("var a = 0; switch(5) { case 1: a=10; break; default: a=99; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 99);
}

TEST_F(RegInterpFixture, SwitchFallthrough) {
  Value v = eval("var a = 0; switch(1) { case 1: a=1; case 2: a=2; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

// ─── Closure Mutation ───────────────────────────────────────────────────────

TEST_F(RegInterpFixture, ClosureMutate) {
  Value v = eval("function outer() { var x = 1; var f = function() { x = x + 1; }; f(); return x; } outer();");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

TEST_F(RegInterpFixture, ClosureMultipleCalls) {
  Value v = eval(
      "function makeCounter() { var n = 0; return function() { n = n + 1; return n; }; } var c = makeCounter(); var a = c(); var b = c(); a + b;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

// ─── Finally ────────────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, FinallyRuns) {
  Value v = eval("var a = 0; try { a = 1; } finally { a = 2; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

TEST_F(RegInterpFixture, FinallyWithCatch) {
  Value v = eval("var a = 0; try { throw 99; } catch(e) { a = e; } finally { a = a + 1; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 100);
}

// ─── Named Labels ───────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, LabeledBreak) {
  Value v = eval("var a=0,i=0; outer:while(i<10){i=i+1;a=i;break outer;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 1);
}

TEST_F(RegInterpFixture, LabeledContinue) {
  Value v = eval("var a=0,i=0; outer:while(i<3){i=i+1;a=i;continue outer;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

// ─── For-Loop Execution ─────────────────────────────────────────────────────

TEST_F(RegInterpFixture, ForLoopExecute) {
  Value v = eval("var a=0; for(var i=0;i<3;i=i+1){a=i;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

TEST_F(RegInterpFixture, ForLoopBreak) {
  Value v = eval("var a=0; for(var i=0;i<10;i=i+1){a=i;if(i>2)break;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

TEST_F(RegInterpFixture, ForLoopContinue) {
  Value v = eval("var a=0; for(var i=0;i<5;i=i+1){if(i==2)continue;a=i;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 4);
}

TEST_F(RegInterpFixture, LabeledForBreak) {
  Value v = eval("var a=0; outer:for(var i=0;i<10;i=i+1){a=i;break outer;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 0);
}

TEST_F(RegInterpFixture, LabeledForContinue) {
  Value v = eval("var a=0; outer:for(var i=0;i<3;i=i+1){a=i;continue outer;} a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
