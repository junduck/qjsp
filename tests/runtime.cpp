#include "qjsp/runtime.hpp"
#include "qjsp/context.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

TEST(RuntimeContext, CreateAndDestroy) {
  auto rt  = std::make_unique<Runtime>();
  auto ctx = std::make_unique<Context>(rt.get());
}

TEST(StrPrimOps, CreateAndCmp) {
  auto *a = StrPrim::allocate_raw("abc");
  auto *b = StrPrim::allocate_raw("abd");
  EXPECT_LT(StrPrim::compare(a, b), 0);
  EXPECT_EQ(a->view(), "abc");
  a->unref();
  b->unref();
}

TEST(AtomIntern, Predefined) {
  auto rt = std::make_unique<Runtime>();
  EXPECT_EQ(rt->atom_view(rt->intern("prototype")), "prototype");
}

TEST(AtomIntern, Dynamic) {
  auto rt = std::make_unique<Runtime>();
  Atom a  = rt->intern("myKey");
  EXPECT_NE(a, kAtomNull);
  EXPECT_EQ(rt->atom_view(a), "myKey");
}
