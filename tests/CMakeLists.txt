add_executable(test_main main.cpp value.cpp)
target_link_libraries(test_main GTest::gtest_main qjsp::qjsp)

