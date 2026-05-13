#include "qjsp/value.hpp"
#include "qjsp/atom.hpp"
#include "qjsp/engine.hpp"
#include <gtest/gtest.h>
#include <memory>

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

TEST(ValueBasics, Float64Inf) {
  double inf = std::numeric_limits<double>::infinity();
  Value v    = Value::float64(inf);
  EXPECT_TRUE(v.is_double());
  EXPECT_DOUBLE_EQ(v.as_double(), inf);
}

TEST(ValueSymbol, RoundTrip) {
  Value v = Value::symbol_from_atom(42);
  EXPECT_TRUE(v.is_symbol());
  EXPECT_FALSE(v.is_pointer());
  EXPECT_EQ(v.as_symbol(), static_cast<Atom>(42));
}

TEST(ValueSymbol, DistinctFromStrPrim) {
  Value sym = Value::symbol_from_atom(1);
  Value str = Value::string(nullptr);
  EXPECT_FALSE(sym.is_string());
  EXPECT_TRUE(sym.is_symbol());
  EXPECT_FALSE(str.is_symbol());
}

TEST(ValueSymbol, NotPointer) {
  Value v = Value::symbol_from_atom(100);
  EXPECT_FALSE(v.is_pointer());
  EXPECT_FALSE(v.is_nullptr());
}

TEST(ValueSymbol, DifferentAtomsAreDistinct) {
  Value a = Value::symbol_from_atom(1);
  Value b = Value::symbol_from_atom(2);
  EXPECT_NE(a.data, b.data);
  EXPECT_EQ(a.as_symbol(), static_cast<Atom>(1));
  EXPECT_EQ(b.as_symbol(), static_cast<Atom>(2));
}

TEST(ValueSymbol, CreateSymbolUnique) {
  auto e = std::make_unique<Engine>();
  Atom a = e->create_symbol("x");
  Atom b = e->create_symbol("x");
  EXPECT_NE(a, b);
  EXPECT_TRUE(e->atom_is_symbol(a));
  EXPECT_TRUE(e->atom_is_symbol(b));
}

TEST(ValueSymbol, PredefSymbolIsAtom) {
  auto e = std::make_unique<Engine>();
  Atom si = e->known[WellKnown::symbol_iterator];
  EXPECT_TRUE(e->atom_is_symbol(si));
}
