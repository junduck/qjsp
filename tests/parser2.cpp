#include "qjsp/parser2.hpp"
#include "qjsp/lexer2.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace qjsp;

struct Parser2Fixture : testing::Test {
    Parser p;

    NodeIndex parse(const char *src) {
        auto len = static_cast<uint32_t>(strlen(src));
        p.init(reinterpret_cast<const uint8_t *>(src), len);
        return p.parse();
    }

    AstTree &tree() { return p.tree(); }
    bool has_errors() const { return !p.tree().errors.empty(); }
    NodeKind kind(NodeIndex n) const { return p.tree().kind(n); }
};

TEST_F(Parser2Fixture, Empty) {
    auto n = parse("");
    EXPECT_NE(n, NodeNull);
    EXPECT_EQ(kind(n), NK_PROGRAM);
}

TEST_F(Parser2Fixture, ExprStmt) {
    auto n = parse("42;");
    EXPECT_EQ(kind(n), NK_PROGRAM);
    auto body = tree().range(n, 0);
    ASSERT_EQ(body.len, 1u);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_EXPR_STMT);
}

TEST_F(Parser2Fixture, BinaryExpr) {
    auto n = parse("1 + 2;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_EXPR_STMT);
    auto expr = tree().d(stmt, 0);
    EXPECT_EQ(kind(expr), NK_BINARY_EXPR);
    EXPECT_EQ(tree().d(expr, 2), BinAdd);
}

TEST_F(Parser2Fixture, VarDecl) {
    auto n = parse("var x = 1;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_VAR_DECL);
    EXPECT_EQ(tree().d(stmt, 2), VarVar);
}

TEST_F(Parser2Fixture, LetDecl) {
    auto n = parse("let y = 2;");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_VAR_DECL);
}

TEST_F(Parser2Fixture, ConstDecl) {
    auto n = parse("const z = 3;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_VAR_DECL);
    EXPECT_EQ(tree().d(stmt, 2), VarConst);
}

TEST_F(Parser2Fixture, IfStmt) {
    auto n = parse("if (true) { 42; }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_IF_STMT);
}

TEST_F(Parser2Fixture, IfElseStmt) {
    auto n = parse("if (false) { 1; } else { 2; }");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_IF_STMT);
    EXPECT_NE(tree().d(stmt, 2), NodeNull);
}

TEST_F(Parser2Fixture, WhileStmt) {
    auto n = parse("while (true) { break; }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_WHILE_STMT);
}

TEST_F(Parser2Fixture, DoWhileStmt) {
    auto n = parse("do { } while (false);");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_DO_WHILE_STMT);
}

TEST_F(Parser2Fixture, ForStmt) {
    auto n = parse("for (var i = 0; i < 10; i = i + 1) { }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_FOR_STMT);
}

TEST_F(Parser2Fixture, ForInStmt) {
    auto n = parse("for (var x in obj) { }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_FOR_IN_STMT);
}

TEST_F(Parser2Fixture, ForOfStmt) {
    auto n = parse("for (var x of arr) { }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_FOR_OF_STMT);
}

TEST_F(Parser2Fixture, FunctionDecl) {
    auto n = parse("function add(a, b) { return a + b; }");
    auto body = tree().range(n, 0);
    auto fn = tree().extra(body)[0];
    EXPECT_EQ(kind(fn), NK_FUNCTION);
    EXPECT_NE(tree().d(fn, 0), NodeNull);
    EXPECT_NE(tree().d(fn, 2), NodeNull);
}

TEST_F(Parser2Fixture, ThrowStmt) {
    auto n = parse("throw 42;");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_THROW_STMT);
}

TEST_F(Parser2Fixture, TryCatch) {
    auto n = parse("try { } catch (e) { }");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_TRY_STMT);
    EXPECT_NE(tree().d(stmt, 1), NodeNull);
}

TEST_F(Parser2Fixture, TryFinally) {
    auto n = parse("try { } finally { }");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_TRY_STMT);
    EXPECT_NE(tree().d(stmt, 2), NodeNull);
}

TEST_F(Parser2Fixture, SwitchStmt) {
    auto n = parse("switch (x) { case 1: break; default: break; }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_SWITCH_STMT);
}

TEST_F(Parser2Fixture, BlockStmt) {
    auto n = parse("{ var x = 1; }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_BLOCK_STMT);
}

TEST_F(Parser2Fixture, ArrowFunction) {
    auto n = parse("var f = (x) => x + 1;");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    auto init = tree().d(tree().extra(tree().range(decl, 0))[0], 1);
    EXPECT_EQ(kind(init), NK_ARROW_FUNCTION);
}

TEST_F(Parser2Fixture, SimpleArrow) {
    auto n = parse("var f = x => x + 1;");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    auto init = tree().d(tree().extra(tree().range(decl, 0))[0], 1);
    EXPECT_EQ(kind(init), NK_ARROW_FUNCTION);
}

TEST_F(Parser2Fixture, ArrayExpr) {
    auto n = parse("[1, 2, 3];");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(stmt), NK_EXPR_STMT);
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_ARRAY_EXPR);
}

TEST_F(Parser2Fixture, ObjectExpr) {
    auto n = parse("({a: 1, b: 2});");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    auto paren = tree().d(stmt, 0);
    EXPECT_EQ(kind(paren), NK_PAREN_EXPR);
    EXPECT_EQ(kind(tree().d(paren, 0)), NK_OBJECT_EXPR);
}

TEST_F(Parser2Fixture, CallExpr) {
    auto n = parse("foo(1, 2);");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_CALL_EXPR);
}

TEST_F(Parser2Fixture, MemberExpr) {
    auto n = parse("a.b;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_MEMBER_EXPR);
}

TEST_F(Parser2Fixture, ConditionalExpr) {
    auto n = parse("x ? 1 : 2;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_CONDITIONAL_EXPR);
}

TEST_F(Parser2Fixture, UnaryExpr) {
    auto n = parse("-x;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_UNARY_EXPR);
}

TEST_F(Parser2Fixture, UpdateExpr) {
    auto n = parse("x++;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_UPDATE_EXPR);
}

TEST_F(Parser2Fixture, SequenceExpr) {
    auto n = parse("1, 2, 3;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_SEQUENCE_EXPR);
}

TEST_F(Parser2Fixture, TemplateLit) {
    auto n = parse("`hello`;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_TEMPLATE_LIT);
}

TEST_F(Parser2Fixture, TemplateLitWithExpr) {
    auto n = parse("`hello ${name}`;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    auto tpl = tree().d(stmt, 0);
    EXPECT_EQ(kind(tpl), NK_TEMPLATE_LIT);
    EXPECT_EQ(tree().d(tpl, 3), 1u);
}

TEST_F(Parser2Fixture, ClassDecl) {
    auto n = parse("class Foo { constructor() {} }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_CLASS);
}

TEST_F(Parser2Fixture, ClassExtends) {
    auto n = parse("class Foo extends Bar {}");
    auto body = tree().range(n, 0);
    auto cls = tree().extra(body)[0];
    EXPECT_EQ(kind(cls), NK_CLASS);
    EXPECT_NE(tree().d(cls, 1), NodeNull);
}

TEST_F(Parser2Fixture, LogicalExpr) {
    auto n = parse("a && b || c ?? d;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_LOGICAL_EXPR);
}

TEST_F(Parser2Fixture, AssignmentExpr) {
    auto n = parse("x = 42;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_ASSIGNMENT_EXPR);
}

TEST_F(Parser2Fixture, NewExpr) {
    auto n = parse("new Foo(1);");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_NEW_EXPR);
}

TEST_F(Parser2Fixture, LabeledStmt) {
    auto n = parse("label: for (;;) { break label; }");
    auto body = tree().range(n, 0);
    EXPECT_EQ(kind(tree().extra(body)[0]), NK_LABELED_STMT);
}

TEST_F(Parser2Fixture, RegExpLiteral) {
    auto n = parse("/abc/;");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(tree().d(stmt, 0)), NK_REGEXP_LIT);
}

TEST_F(Parser2Fixture, MultipleVarDeclarators) {
    auto n = parse("var a = 1, b = 2, c = 3;");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_VAR_DECL);
    auto decls = tree().range(decl, 0);
    EXPECT_EQ(decls.len, 3u);
}

TEST_F(Parser2Fixture, HexLiteral) {
    auto n = parse("0xFF;");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_NUMERIC_LIT);
}

TEST_F(Parser2Fixture, OctalLiteral) {
    auto n = parse("0o77;");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_NUMERIC_LIT);
}

TEST_F(Parser2Fixture, BinaryLiteral) {
    auto n = parse("0b1010;");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_NUMERIC_LIT);
}

TEST_F(Parser2Fixture, Hashbang) {
    auto n = parse("#!/usr/bin/env node\nvar x = 1;");
    EXPECT_NE(n, NodeNull);
    auto body = tree().range(n, 0);
    EXPECT_EQ(body.len, 1u);
}

TEST_F(Parser2Fixture, AsyncKeyword) {
    auto n = parse("async function f() {}");
    auto body = tree().range(n, 0);
    auto fn = tree().extra(body)[0];
    EXPECT_EQ(kind(fn), NK_FUNCTION);
}

TEST_F(Parser2Fixture, StringEscapes) {
    auto n = parse("\"hello\\nworld\";");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_STRING_LIT);
}

TEST_F(Parser2Fixture, StringHexEscape) {
    auto n = parse("\"\\x41\";");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_STRING_LIT);
}

TEST_F(Parser2Fixture, StringUnicodeEscape) {
    auto n = parse("\"\\u0041\";");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_STRING_LIT);
}

TEST_F(Parser2Fixture, RegexFlags) {
    auto n = parse("/abc/gimsuy;");
    auto body = tree().range(n, 0);
    auto lit = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(lit), NK_REGEXP_LIT);
}

TEST_F(Parser2Fixture, ImportSideEffect) {
    auto n = parse("import 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_IMPORT_DECL);
}

TEST_F(Parser2Fixture, ImportDefault) {
    auto n = parse("import foo from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_IMPORT_DECL);
    auto specs = tree().range(decl, 0);
    EXPECT_EQ(specs.len, 1u);
    EXPECT_EQ(kind(tree().extra(specs)[0]), NK_IMPORT_DEFAULT);
}

TEST_F(Parser2Fixture, ImportNamed) {
    auto n = parse("import { foo, bar as baz } from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_IMPORT_DECL);
    auto specs = tree().range(decl, 0);
    EXPECT_EQ(specs.len, 2u);
    EXPECT_EQ(kind(tree().extra(specs)[0]), NK_IMPORT_SPEC);
    EXPECT_EQ(kind(tree().extra(specs)[1]), NK_IMPORT_SPEC);
}

TEST_F(Parser2Fixture, ImportNamespace) {
    auto n = parse("import * as foo from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_IMPORT_DECL);
    auto specs = tree().range(decl, 0);
    EXPECT_EQ(specs.len, 1u);
    EXPECT_EQ(kind(tree().extra(specs)[0]), NK_IMPORT_NAMESPACE);
}

TEST_F(Parser2Fixture, ImportDefaultAndNamed) {
    auto n = parse("import foo, { bar } from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    auto specs = tree().range(decl, 0);
    EXPECT_EQ(specs.len, 2u);
    EXPECT_EQ(kind(tree().extra(specs)[0]), NK_IMPORT_DEFAULT);
    EXPECT_EQ(kind(tree().extra(specs)[1]), NK_IMPORT_SPEC);
}

TEST_F(Parser2Fixture, ImportDefaultAndNamespace) {
    auto n = parse("import foo, * as bar from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    auto specs = tree().range(decl, 0);
    EXPECT_EQ(specs.len, 2u);
    EXPECT_EQ(kind(tree().extra(specs)[0]), NK_IMPORT_DEFAULT);
    EXPECT_EQ(kind(tree().extra(specs)[1]), NK_IMPORT_NAMESPACE);
}

TEST_F(Parser2Fixture, ImportExpression) {
    auto n = parse("import('module');");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_IMPORT_EXPR);
}

TEST_F(Parser2Fixture, ExportNamedSpecifiers) {
    auto n = parse("export { foo, bar as baz };");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_NAMED);
    auto specs = tree().range(decl, 1);
    EXPECT_EQ(specs.len, 2u);
    EXPECT_EQ(kind(tree().extra(specs)[0]), NK_EXPORT_SPEC);
    EXPECT_EQ(kind(tree().extra(specs)[1]), NK_EXPORT_SPEC);
}

TEST_F(Parser2Fixture, ExportNamedFromSource) {
    auto n = parse("export { foo } from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_NAMED);
    EXPECT_NE(tree().d(decl, 3), NodeNull);
}

TEST_F(Parser2Fixture, ExportDeclaration) {
    auto n = parse("export function foo() {}");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_NAMED);
    auto inner = tree().d(decl, 0);
    EXPECT_EQ(kind(inner), NK_FUNCTION);
}

TEST_F(Parser2Fixture, ExportDefaultFunction) {
    auto n = parse("export default function foo() {}");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_DEFAULT);
    auto inner = tree().d(decl, 0);
    EXPECT_EQ(kind(inner), NK_FUNCTION);
}

TEST_F(Parser2Fixture, ExportDefaultExpression) {
    auto n = parse("export default 42;");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_DEFAULT);
}

TEST_F(Parser2Fixture, ExportAll) {
    auto n = parse("export * from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_ALL);
}

TEST_F(Parser2Fixture, ExportAllAs) {
    auto n = parse("export * as foo from 'module';");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_ALL);
}

TEST_F(Parser2Fixture, OptionalChainContinuation) {
    auto n = parse("a?.b.c;");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_CHAIN_EXPR);
}

TEST_F(Parser2Fixture, PrivateFieldAccess) {
    auto n = parse("class C { #x; foo() { return this.#x; } }");
    EXPECT_NE(n, NodeNull);
}

TEST_F(Parser2Fixture, AsyncArrowAwait) {
    auto n = parse("async () => await x;");
    auto body = tree().range(n, 0);
    auto expr_stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(expr_stmt), NK_EXPR_STMT);
    auto arrow = tree().d(expr_stmt, 0);
    EXPECT_EQ(kind(arrow), NK_ARROW_FUNCTION);
    EXPECT_TRUE(tree().d(arrow, 3) & NF::Async);
}

TEST_F(Parser2Fixture, AsyncArrowSingleParam) {
    auto n = parse("async x => x;");
    auto body = tree().range(n, 0);
    auto expr_stmt = tree().extra(body)[0];
    EXPECT_EQ(kind(expr_stmt), NK_EXPR_STMT);
    auto arrow = tree().d(expr_stmt, 0);
    EXPECT_EQ(kind(arrow), NK_ARROW_FUNCTION);
    EXPECT_TRUE(tree().d(arrow, 3) & NF::Async);
}

TEST_F(Parser2Fixture, ForAwaitOf) {
    auto n = parse("async function f() { for await (const x of y) {} }");
    EXPECT_NE(n, NodeNull);
}

TEST_F(Parser2Fixture, InInComputedMember) {
    auto n = parse("for (a[b in c]; ; ) {}");
    EXPECT_NE(n, NodeNull);
}

TEST_F(Parser2Fixture, ExportVar) {
    auto n = parse("export const x = 1;");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_NAMED);
    auto inner = tree().d(decl, 0);
    EXPECT_EQ(kind(inner), NK_VAR_DECL);
}

TEST_F(Parser2Fixture, ExportClass) {
    auto n = parse("export class Foo {}");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_NAMED);
}

TEST_F(Parser2Fixture, ExportAsyncFunction) {
    auto n = parse("export async function foo() {}");
    auto body = tree().range(n, 0);
    auto decl = tree().extra(body)[0];
    EXPECT_EQ(kind(decl), NK_EXPORT_NAMED);
    auto fn = tree().d(decl, 0);
    EXPECT_EQ(kind(fn), NK_FUNCTION);
    EXPECT_TRUE(tree().d(fn, 3) & NF::Async);
}

TEST_F(Parser2Fixture, ImportExpressionInCall) {
    auto n = parse("x(import('module'));");
    auto body = tree().range(n, 0);
    auto call = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(call), NK_CALL_EXPR);
}

TEST_F(Parser2Fixture, ThrowNewlineIsError) {
    auto n = parse("throw\n42;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ArrowDisablesYield) {
    auto n = parse("function* g() { var f = () => yield; }");
    EXPECT_NE(n, NodeNull);
}

TEST_F(Parser2Fixture, SuperFollowToken) {
    auto n = parse("function f() { super(); }");
    EXPECT_NE(n, NodeNull);
    auto n2 = parse("function f() { super.foo; }");
    EXPECT_NE(n2, NodeNull);
}

TEST_F(Parser2Fixture, SuperAloneIsError) {
    auto n = parse("function f() { super + 1; }");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, PrivateIdentBareIsError) {
    auto n = parse("#foo;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ForAwaitOnlyWithOf) {
    auto n = parse("async function f() { for await (const x of y) {} }");
    EXPECT_NE(n, NodeNull);
    auto n2 = parse("async function f() { for await (const x in y) {} }");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ForAwaitClassicForIsError) {
    auto n = parse("async function f() { for await (;;) {} }");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, UnaryPowError) {
    auto n = parse("-2 ** 3;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, UnaryPowParenOk) {
    auto n = parse("(-2) ** 3;");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_BINARY_EXPR);
}

TEST_F(Parser2Fixture, AwaitPowError) {
    auto n = parse("async function f() { await 2 ** 3; }");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, NullishLogicalMixError) {
    auto n = parse("a ?? b || c;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, LogicalNullishMixError) {
    auto n = parse("a && b ?? c;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, NullishLogicalParensOk) {
    auto n = parse("a ?? (b || c);");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_LOGICAL_EXPR);
}

TEST_F(Parser2Fixture, NewTarget) {
    auto n = parse("function f() { new.target; }");
    EXPECT_NE(n, NodeNull);
}

TEST_F(Parser2Fixture, NewOtherPropertyError) {
    auto n = parse("function f() { new.foo; }");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ImportMeta) {
    auto n = parse("import.meta;");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_META_PROPERTY);
}

TEST_F(Parser2Fixture, ImportOtherPropertyError) {
    auto n = parse("import.foo;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, AssignmentLHSIdent) {
    auto n = parse("x = 1;");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_ASSIGNMENT_EXPR);
}

TEST_F(Parser2Fixture, AssignmentLHSDestructure) {
    auto n = parse("[a, b] = [1, 2];");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_ASSIGNMENT_EXPR);
    auto left = tree().d(expr, 0);
    EXPECT_EQ(kind(left), NK_ARRAY_PAT);
}

TEST_F(Parser2Fixture, CompoundAssignmentLHSMember) {
    auto n = parse("obj.x += 1;");
    auto body = tree().range(n, 0);
    auto expr = tree().d(tree().extra(body)[0], 0);
    EXPECT_EQ(kind(expr), NK_ASSIGNMENT_EXPR);
}

TEST_F(Parser2Fixture, CompoundAssignmentLHSLiteralError) {
    auto n = parse("5 += 1;");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ForInitConstNoInitError) {
    auto n = parse("for (const x;;) {}");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ForInitDestructureNoInitError) {
    auto n = parse("for (var [a, b];;) {}");
    EXPECT_TRUE(tree().errors.size() > 0);
}

TEST_F(Parser2Fixture, ForInitConstWithInitOk) {
    auto n = parse("for (const x = 1;;) {}");
    EXPECT_NE(n, NodeNull);
}

TEST_F(Parser2Fixture, ObjectMethodShorthand) {
    auto n = parse("({ foo() { return 1; } });");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ObjectGetterSetter) {
    auto n = parse("({ get x() { return 1; }, set x(v) {} });");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ObjectGeneratorMethod) {
    auto n = parse("({ *gen() { yield 1; } });");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ObjectComputedMethod) {
    auto n = parse("({ [sym]() {} });");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ClassStaticBlock) {
    auto n = parse("class A { static { this.x = 1; } }");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ClassStaticMethodNamedStatic) {
    auto n = parse("class A { static() {} }");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ClassStaticGetter) {
    auto n = parse("class A { static get foo() { return 1; } }");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ClassStaticProperty) {
    auto n = parse("class A { static x = 1; }");
    EXPECT_NE(n, NodeNull);
    EXPECT_TRUE(tree().errors.empty());
}

TEST_F(Parser2Fixture, ObjectGetMethodAsProperty) {
    auto n = parse("({ get: 1 });");
    auto body = tree().range(n, 0);
    auto stmt = tree().extra(body)[0];
    auto paren = tree().d(stmt, 0);
    auto obj = tree().d(paren, 0);
    auto props = tree().range(obj, 0);
    auto prop = tree().extra(props)[0];
    EXPECT_EQ(kind(prop), NK_OBJECT_PROP);
}
