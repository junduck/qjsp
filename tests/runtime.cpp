#include "qjsp/engine.hpp"
#include "qjsp/string.hpp"
#include "qjsp/value.hpp"
#include <cstring>
#include <gtest/gtest.h>
#include <memory>

using namespace qjsp;

TEST(EngineCreate, CreateAndDestroy) {
  auto e = std::make_unique<Engine>();
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
  auto e = std::make_unique<Engine>();
  EXPECT_EQ(e->atom_view(e->intern("prototype")), "prototype");
}

TEST(AtomIntern, Dynamic) {
  auto e = std::make_unique<Engine>();
  Atom a = e->intern("myKey");
  EXPECT_NE(a, kAtomNull);
  EXPECT_EQ(e->atom_view(a), "myKey");
}
