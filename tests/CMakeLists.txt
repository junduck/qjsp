add_executable(test_main
  main.cpp
  value.cpp
  runtime.cpp
  object.cpp
  gc_adversarial.cpp
  lexer.cpp
  parser.cpp
  interpreter.cpp
)
target_link_libraries(test_main GTest::gtest_main qjsp::qjsp)

