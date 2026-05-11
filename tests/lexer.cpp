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
  EXPECT_EQ(lexer.token.kind, TokenKind::Eof);
}

TEST_F(LexerFixture, SingleCharTokens) {
  init_lexer("(){}[],;:");
  for (TokenKind expected : {TokenKind::LParen, TokenKind::RParen, TokenKind::LBrace, TokenKind::RBrace,
                              TokenKind::LBracket, TokenKind::RBracket, TokenKind::Comma, TokenKind::Semicolon,
                              TokenKind::Colon}) {
    EXPECT_TRUE(lexer.next_token());
    EXPECT_EQ(lexer.token.kind, expected);
  }
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Eof);
}

TEST_F(LexerFixture, Identifiers) {
  init_lexer("foo bar _x $y");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "foo");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "bar");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "_x");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(rt->atom_view(lexer.token.ident_atom), "$y");
}

TEST_F(LexerFixture, Keywords) {
  init_lexer("if else return var function");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwIf);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwElse);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwReturn);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwVar);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwFunction);
}

TEST_F(LexerFixture, StringLiterals) {
  init_lexer("\"hello\" 'world'");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "hello");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "world");
}

TEST_F(LexerFixture, StringEscapes) {
  init_lexer("\"a\\nb\\tc\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "a\nb\tc");
}

TEST_F(LexerFixture, Numbers) {
  init_lexer("42 3.14 0xFF");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Number);
  EXPECT_DOUBLE_EQ(lexer.token.num_val, 42.0);

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Number);
  EXPECT_DOUBLE_EQ(lexer.token.num_val, 3.14);

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Number);
  EXPECT_DOUBLE_EQ(lexer.token.num_val, 255.0);
}

TEST_F(LexerFixture, Operators) {
  init_lexer("+ - * / % == === != !== < > <= >= && || ?? ?.");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Plus);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Minus);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Star);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::SlashChar);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Percent);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Eq);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StrictEq);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Neq);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StrictNeq);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Less);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Greater);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Lte);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Gte);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Land);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Lor);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::DoubleQuestionMark);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::QuestionMarkDot);
}

TEST_F(LexerFixture, ArrowAndEllipsis) {
  init_lexer("=> ...");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Arrow);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Ellipsis);
}

TEST_F(LexerFixture, AssignmentOperators) {
  init_lexer("= += -= *= /= %= <<= >>= >>>= &= |= ^= &&= ||= ??= **= **");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::EqAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::PlusAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::MinusAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::MulAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::DivAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::ModAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::ShlAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::SarAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::ShrAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::AndAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::OrAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::XorAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::LandAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::LorAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::DoubleQuestionMarkAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::PowAssign);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Pow);
}

TEST_F(LexerFixture, IncrementDecrement) {
  init_lexer("++ --");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Inc);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Dec);
}

TEST_F(LexerFixture, TemplateLiteral) {
  init_lexer("`hello`");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::TemplateLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "hello");
  EXPECT_EQ(lexer.token.str_sep, '`');
}

TEST_F(LexerFixture, TemplateLiteralEscapes) {
  // \n should be a newline, not literal \ + n
  init_lexer("`a\\nb`");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::TemplateLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "a\nb");
}

TEST_F(LexerFixture, StringHexEscape) {
  init_lexer("\"\\x41\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "A");
}

TEST_F(LexerFixture, StringUnicodeEscape) {
  init_lexer("\"\\u0041\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "A");
}

TEST_F(LexerFixture, CommentSkip) {
  init_lexer("a /* block */ b // line\na c");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
}

TEST_F(LexerFixture, PeekToken) {
  init_lexer("in of import export function");
  EXPECT_EQ(lexer.peek_token(false), TokenKind::KwIn);
  EXPECT_EQ(lexer.peek_token(true), TokenKind::KwIn);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwIn);
  EXPECT_EQ(lexer.peek_token(false), TokenKind::KwOf);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwOf);
  EXPECT_EQ(lexer.peek_token(false), TokenKind::KwImport);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwImport);
  EXPECT_EQ(lexer.peek_token(false), TokenKind::KwExport);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::KwExport);
  EXPECT_EQ(lexer.peek_token(false), TokenKind::KwFunction);
}

TEST_F(LexerFixture, LookaheadArrow) {
  init_lexer("=>");
  EXPECT_EQ(lexer.peek_token(true), TokenKind::Arrow);
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Arrow);
}
