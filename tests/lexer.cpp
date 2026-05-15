#include "qjsp/engine.hpp"
#include "qjsp/lexer.hpp"
#include "qjsp/object.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>

using namespace qjsp;

struct LexerFixture : testing::Test {
  std::unique_ptr<Engine> e = std::make_unique<Engine>();
  Lexer lexer;

  void init_lexer(const char *source) { lexer.init(e.get(), "test.js", reinterpret_cast<const uint8_t *>(source), std::strlen(source)); }
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
  EXPECT_EQ(e->atom_view(lexer.token.ident_atom), "foo");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(e->atom_view(lexer.token.ident_atom), "bar");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(e->atom_view(lexer.token.ident_atom), "_x");

  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_EQ(e->atom_view(lexer.token.ident_atom), "$y");
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

TEST_F(LexerFixture, UnicodeIdent) {
  // Valid Unicode identifier (Greek letter) should be accepted
  init_lexer("\u03b1"); // Greek alpha
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
}

TEST_F(LexerFixture, UnicodeIdentInvalid) {
  // © is not ID_Start — should be rejected or produce error
  init_lexer("\u00a9"); // copyright sign
  // Not a valid identifier start; the lexer should not produce Identifier
  bool ok = lexer.next_token();
  if (ok) {
    EXPECT_NE(lexer.token.kind, TokenKind::Identifier);
  }
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

// ── Escape support verification ──────────────────────────────────────────────

// \xHH in strings
TEST_F(LexerFixture, EscapeHexInString) {
  init_lexer("\"\\x00\\x41\\xFF\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  ASSERT_EQ(lexer.token.str_val.size(), 4u);
  EXPECT_EQ(lexer.token.str_val[0], '\x00');
  EXPECT_EQ(lexer.token.str_val[1], 'A');
  EXPECT_EQ(static_cast<unsigned char>(lexer.token.str_val[2]), 0xC3);
  EXPECT_EQ(static_cast<unsigned char>(lexer.token.str_val[3]), 0xBF);
}

// \xHH in templates
TEST_F(LexerFixture, EscapeHexInTemplate) {
  init_lexer("`\\x41`");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::TemplateLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "A");
}

// \uXXXX in strings
TEST_F(LexerFixture, EscapeUnicode4InString) {
  init_lexer("\"\\u0041\\u00e9\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "A\xc3\xa9");
}

// \uXXXX in templates
TEST_F(LexerFixture, EscapeUnicode4InTemplate) {
  init_lexer("`\\u0041`");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::TemplateLit);
  EXPECT_STREQ(lexer.token.str_val.c_str(), "A");
}

// \u{...} in strings (ES6 Unicode code point escape)
TEST_F(LexerFixture, EscapeUnicodeBraceInString) {
  init_lexer("\"\\u{41}\\u{1F600}\\u{0}\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_EQ(lexer.token.str_val.size(), 1u + 4u + 1u);
  EXPECT_EQ(lexer.token.str_val[0], 'A');
}

// \u{...} in templates
TEST_F(LexerFixture, EscapeUnicodeBraceInTemplate) {
  init_lexer("`\\u{1F600}`");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::TemplateLit);
  EXPECT_EQ(lexer.token.str_val.size(), 4u);
}

// \uXXXX as identifier start
TEST_F(LexerFixture, EscapeUnicodeIdentStart) {
  init_lexer("\\u{3B1}"); // Greek alpha
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_TRUE(lexer.token.ident_has_escape);
}

// \uXXXX in identifier body
TEST_F(LexerFixture, EscapeUnicodeIdentBody) {
  init_lexer("a\\u{0327}"); // a + combining cedilla
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::Identifier);
  EXPECT_TRUE(lexer.token.ident_has_escape);
}

// \x with bad hex digits should fail
TEST_F(LexerFixture, EscapeHexBadDigit) {
  init_lexer("\"\\xGG\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidEscape);
}

// \x at end of string should fail
TEST_F(LexerFixture, EscapeHexTruncated) {
  init_lexer("\"\\x4\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidEscape);
}

// \u with bad hex should fail
TEST_F(LexerFixture, EscapeUnicodeBadHex) {
  init_lexer("\"\\uGGGG\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidEscape);
}

// \u{} with codepoint > 10FFFF should fail
TEST_F(LexerFixture, EscapeUnicodeOverflow) {
  init_lexer("\"\\u{110000}\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidEscape);
}

// \u{} empty braces should fail
TEST_F(LexerFixture, EscapeUnicodeEmptyBrace) {
  init_lexer("\"\\u{}\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidEscape);
}

// \0 is valid in strict mode (NUL escape), \1-\7 are NOT
TEST_F(LexerFixture, EscapeNulIsValid) {
  init_lexer("\"\\0\"");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_EQ(lexer.token.kind, TokenKind::StringLit);
  EXPECT_EQ(lexer.token.str_val.size(), 1u);
  EXPECT_EQ(lexer.token.str_val[0], '\x00');
}

TEST_F(LexerFixture, EscapeOctal1IsRejected) {
  init_lexer("\"\\1\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidOctalEscape);
}

TEST_F(LexerFixture, EscapeOctal7IsRejected) {
  init_lexer("\"\\7\"");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidOctalEscape);
}

// \8 and \9 in templates should fail
TEST_F(LexerFixture, Escape8InTemplateRejected) {
  init_lexer("`\\8`");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidOctalEscape);
}

TEST_F(LexerFixture, ErrorKindUnterminatedString) {
  init_lexer("\"unterminated");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_TRUE(lexer.error_);
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::UnterminatedString);
  EXPECT_GT(lexer.error_.offset, 0u);
}

TEST_F(LexerFixture, ErrorKindUnterminatedComment) {
  init_lexer("/* never ends");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_TRUE(lexer.error_);
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::UnterminatedComment);
}

TEST_F(LexerFixture, ErrorKindInvalidNumber) {
  init_lexer("0o");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_TRUE(lexer.error_);
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::InvalidNumber);
}

TEST_F(LexerFixture, ErrorKindUnexpectedChar) {
  init_lexer("\x80\x01");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_TRUE(lexer.error_);
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::UnexpectedChar);
}

TEST_F(LexerFixture, ErrorClearedOnSuccess) {
  init_lexer("\"bad");
  EXPECT_FALSE(lexer.next_token());
  EXPECT_TRUE(lexer.error_);
  init_lexer("42");
  EXPECT_TRUE(lexer.next_token());
  EXPECT_FALSE(lexer.error_);
  EXPECT_EQ(lexer.error_.kind, LexErrorKind::None);
}

// ── Bug-reproduction tests ──────────────────────────────────────────────────

// BUG: "0x" (hex prefix with no digits) should be a syntax error, but strtod
//      silently parses it as decimal 0, leaving 'x' for the next token.
TEST_F(LexerFixture, Bug_HexPrefixWithoutDigits_ShouldFail) {
  init_lexer("0x");
  bool ok = lexer.next_token();
  EXPECT_FALSE(ok) << "expected '0x' with no hex digits to fail";
}

// BUG: "0xg" should be a syntax error — strtod parses just the "0" and
//      treats 'xg' as the next token.
TEST_F(LexerFixture, Bug_HexPrefixWithInvalidDigit_ShouldFail) {
  init_lexer("0xg");
  bool ok = lexer.next_token();
  EXPECT_FALSE(ok) << "expected '0xg' to fail";
}

// BUG: Numeric separators in decimal numbers are not supported (ES2021).
//      strtod stops at '_' then the '_' triggers the id_continue check,
//      causing the entire literal to be rejected instead of parsed as 1000.
TEST_F(LexerFixture, Bug_NumericSeparatorDecimal) {
  init_lexer("1_000");
  bool ok = lexer.next_token();
  EXPECT_TRUE(ok) << "1_000 should be a valid numeric literal (value 1000)";
  if (ok) {
    EXPECT_EQ(lexer.token.kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lexer.token.num_val, 1000.0);
  }
}

// Numeric separators in hex literals also unsupported.
TEST_F(LexerFixture, Bug_NumericSeparatorHex) {
  init_lexer("0xFF_FF");
  bool ok = lexer.next_token();
  EXPECT_TRUE(ok) << "0xFF_FF should be a valid hex literal (value 65535)";
  if (ok) {
    EXPECT_EQ(lexer.token.kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lexer.token.num_val, 65535.0);
  }
}
