#include "qjsp/context.hpp"
#include "qjsp/reg_interpreter.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

struct RegInterpFixture : testing::Test {
  std::unique_ptr<Runtime> rt  = std::make_unique<Runtime>();
  std::unique_ptr<Context> ctx = std::make_unique<Context>(rt.get());
  RegInterpreter interp{ctx.get()};

  Value eval(const char *source) { return interp.eval_source(source); }
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

// ─── For-Of Loop ─────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, ForOfSimple) {
  Value v = eval("var arr = [1, 2, 3]; var sum = 0; for (var x of arr) { sum = sum + x; } sum;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 6);
}

// ─── Destructuring ───────────────────────────────────────────────────────

TEST_F(RegInterpFixture, DestructureArray) {
  Value v = eval("var arr = [10, 20]; var [a, b] = arr; a + b;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 30);
}

TEST_F(RegInterpFixture, DestructureObject) {
  Value v = eval("var obj = {x: 1, y: 2}; var {x, y} = obj; x + y;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

TEST_F(RegInterpFixture, DestructureObjectRename) {
  Value v = eval("var obj = {x: 10}; var {x: a} = obj; a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 10);
}

TEST_F(RegInterpFixture, DestructureArrayElision) {
  Value v = eval("var arr = [1, 2, 3]; var [a, , b] = arr; a + b;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 4);
}

TEST_F(RegInterpFixture, DestructureEmpty) {
  Value v = eval("var arr = [1]; var [a] = arr; a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 1);
}

TEST_F(RegInterpFixture, DestructureArrayNested) {
  Value v = eval("var arr = [1, [2, 3]]; var [a, [b, c]] = arr; a + b + c;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 6);
}

TEST_F(RegInterpFixture, DestructureArrayDeepNested) {
  Value v = eval("var arr = [[1, 2], 3]; var [[a, b], c] = arr; a + b + c;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 6);
}

// ─── Try / Catch ────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, TryCatchCaught) {
  Value v = eval("var a = 0; try { throw 99; } catch(e) { a = e; } a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 99);
}

// ─── Multi-frame Exception Propagation ─────────────────────────────────

TEST_F(RegInterpFixture, TryCatchAcrossFunction) {
  Value v = eval(
      "function inner() { throw 42; }"
      "var a = 0;"
      "try { inner(); } catch(e) { a = e; }"
      "a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 42);
}

TEST_F(RegInterpFixture, TryCatchDeepUnwind) {
  Value v = eval(
      "function a() { throw 7; }"
      "function b() { a(); }"
      "var c = 0;"
      "try { b(); } catch(e) { c = e; }"
      "c;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 7);
}

TEST_F(RegInterpFixture, TryCatchNotEnteredOnNormal) {
  Value v = eval(
      "function ok() { return 99; }"
      "var a = 0;"
      "try { a = ok(); } catch(e) { a = -1; }"
      "a;");
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

TEST_F(RegInterpFixture, FinallyOnReturn) {
  Value v = eval("function f() { try { return 1; } finally { return 2; } } f();");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

TEST_F(RegInterpFixture, FinallyOnReturnMutates) {
  Value v = eval(
      "var a = 0;"
      "function f() { try { return 5; } finally { a = 1; } }"
      "var r = f(); a + r;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 6);
}

TEST_F(RegInterpFixture, FinallyOnBreakInFor) {
  Value v = eval(
      "var a = 0;"
      "for (var i = 0; i < 10; i = i + 1) {"
      "  try { a = i; break; } finally { a = a + 1; }"
      "}"
      "a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 1);
}

TEST_F(RegInterpFixture, FinallyOnContinueInFor) {
  Value v = eval(
      "var a = 0;"
      "for (var i = 0; i < 5; i = i + 1) {"
      "  try { if (i == 2) continue; a = i; } finally { a = a + 10; }"
      "}"
      "a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 14);
}

TEST_F(RegInterpFixture, FinallyNoCatchRethrow) {
  Value v = eval(
      "var a = 0;"
      "try { try { throw 7; } finally { a = 1; } } catch(e) { a = e; }"
      "a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 7);
}

TEST_F(RegInterpFixture, NestedTryFinally) {
  Value v = eval(
      "var a = 0;"
      "try {"
      "  try { a = 1; } finally { a = a * 10; }"
      "} finally {"
      "  a = a + 1;"
      "}"
      "a;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 11);
}

TEST_F(RegInterpFixture, FinallyOnReturnNested) {
  Value v = eval(
      "var a = 0;"
      "function f() {"
      "  try {"
      "    try { return 1; } finally { a = a + 10; }"
      "  } finally {"
      "    a = a + 100;"
      "  }"
      "}"
      "var r = f(); a + r;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 111);
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

TEST_F(RegInterpFixture, ForLoopNoTest) {
  // no test condition + update → update relocation must emit JMP back to body
  Value v = eval("var sum=0; for(var i=0;;i++){if(i>=3)break;sum+=i;} sum;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

TEST_F(RegInterpFixture, ForLoopNoTestWithContinue) {
  Value v = eval("var sum=0; for(var i=0;;i++){if(i>=3)break;if(i==1)continue;sum+=i;} sum;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 2);
}

TEST_F(RegInterpFixture, ForLoopNoInitNoTest) {
  Value v = eval("var sum=0; var i=0; for(;;i++){if(i>=3)break;sum+=i;} sum;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

// ─── For-In Loop ─────────────────────────────────────────────────────────

TEST_F(RegInterpFixture, ForInBasic) {
  Value v = eval("var obj = {a: 1, b: 2}; var sum = 0; for (var k in obj) { sum = sum + obj[k]; } sum;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}

TEST_F(RegInterpFixture, ForInCount) {
  Value v = eval("var obj = {x: 10, y: 20, z: 30}; var count = 0; for (var k in obj) { count = count + 1; } count;");
  EXPECT_TRUE(v.is_int32());
  EXPECT_EQ(v.as_int32(), 3);
}
