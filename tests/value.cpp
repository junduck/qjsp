#include "qjsp/value.hpp"
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

TEST(ValueBasics, Float64Inf) {
  double inf  = std::numeric_limits<double>::infinity();
  Value  v    = Value::float64(inf);
  EXPECT_TRUE(v.is_double());
  EXPECT_DOUBLE_EQ(v.as_double(), inf);
}

TEST(ValueSymbol, RoundTrip) {
  Value v = Value::symbol_from_atom(42);
  EXPECT_TRUE(v.is_symbol());
  EXPECT_FALSE(v.is_pointer());
  EXPECT_FALSE(v.has_ref_count());
  EXPECT_EQ(v.as_symbol(), static_cast<Atom>(42));
}

TEST(ValueSymbol, DistinctFromString) {
  // Symbol(1) should not be confused with String
  Value sym  = Value::symbol_from_atom(1);
  Value str  = Value::string(nullptr);
  Value null = Value::null_();
  EXPECT_FALSE(sym.is_string());
  EXPECT_TRUE(sym.is_symbol());
  // null pointer string should not match symbol
  EXPECT_FALSE(str.is_symbol());
}

TEST(ValueSymbol, NotPointer) {
  Value v = Value::symbol_from_atom(100);
  EXPECT_FALSE(v.is_pointer());
  EXPECT_FALSE(v.is_null_ptr());
}

TEST(ValueSymbol, DifferentAtomsAreDistinct) {
  Value a = Value::symbol_from_atom(1);
  Value b = Value::symbol_from_atom(2);
  EXPECT_NE(a.data, b.data);
  EXPECT_EQ(a.as_symbol(), static_cast<Atom>(1));
  EXPECT_EQ(b.as_symbol(), static_cast<Atom>(2));
}
