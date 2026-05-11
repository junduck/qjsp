include(FetchContent)

FetchContent_Declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2.git
  GIT_TAG        2025-11-05
)

FetchContent_MakeAvailable(re2)
