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


// ─── Lexer ──────────────────────────────────────────────────────────────────

struct LexerFixture : testing::Test {
  std::unique_ptr<Runtime> rt = std::make_unique<Runtime>();
  Lexer lexer;

  void init_lexer(const char *source) { lexer.init(rt.get(), "test.js", reinterpret_cast<const uint8_t *>(source), std::strlen(source)); }
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
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "foo");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "bar");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "_x");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_IDENT);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "$y");
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
  EXPECT_STREQ(lexer.token.str_val.c_str(), "hello");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRING);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "world");
}

TEST_F(LexerFixture, StringEscapes) {
  init_lexer("\"a\\nb\\tc\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_STRING);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "a\nb\tc");
}

TEST_F(LexerFixture, Numbers) {
  init_lexer("42 3.14 0xFF");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NUMBER);
  EXPECT_DOUBLE_EQ(lexer.token.num_val, 42.0);

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NUMBER);
  EXPECT_DOUBLE_EQ(lexer.token.num_val, 3.14);

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.type, TOK_NUMBER);
  EXPECT_DOUBLE_EQ(lexer.token.num_val, 255.0);
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
  EXPECT_STREQ(lexer.token.str_val.c_str(), "hello");
  EXPECT_EQ(lexer.token.str_sep, '`');
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
