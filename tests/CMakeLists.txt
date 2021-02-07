find_package(Threads REQUIRED)

add_executable(asiochan_tests)
add_test(
  NAME asiochan_tests
  COMMAND asiochan_tests
)
target_include_directories(
  asiochan_tests

  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(
  asiochan_tests

  PRIVATE
  CONAN_PKG::catch2
  Threads::Threads
  asiochan::asiochan
)

add_subdirectory(asiochan)
