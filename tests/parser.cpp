#include "qjsp/context.hpp"
#include "qjsp/reg_parser.hpp"
#include "qjsp/runtime.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

struct RegParserFixture : testing::Test {
  std::unique_ptr<Runtime> rt  = std::make_unique<Runtime>();
  std::unique_ptr<Context> ctx = std::make_unique<Context>(rt.get());
  RegParseState ps{rt.get(), ctx.get()};

  bool compile(const char *source) {
    ps.init(source, "test.js");
    return ps.compile();
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
