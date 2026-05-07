#include <gtest/gtest.h>

int main(int argc, char **argv) {
  // 1. Initialize GoogleTest (handles --gtest_filter, etc.)
  testing::InitGoogleTest(&argc, argv);

  // 2. Perform custom setup here (optional)

  // 3. Run all tests and return the result
  return RUN_ALL_TESTS();
}
