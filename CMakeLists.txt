cmake_minimum_required(VERSION 3.17)
set(ASIOCHAN_VERSION 0.1.0)
project(AsioChan VERSION "${STRING_ADAPTER_VERSION}")

option(ASIOCHAN_USE_STANDALONE_ASIO "Use standalone ASIO instead of Boost.ASIO" OFF)

add_library(asiochan INTERFACE)
add_library(asiochan::asiochan ALIAS asiochan)
target_compile_features(asiochan INTERFACE cxx_std_20)
target_include_directories(asiochan INTERFACE include)

if (ASIOCHAN_USE_STANDALONE_ASIO)
  target_compile_definitions(asiochan INTERFACE ASIOCHAN_USE_STANDALONE_ASIO)
endif()

# Building the tests requires Conan packages
set(CONAN_BUILD_INFO_PATH "${CMAKE_CURRENT_BINARY_DIR}/conanbuildinfo.cmake")
if (EXISTS "${CONAN_BUILD_INFO_PATH}")
  include("${CONAN_BUILD_INFO_PATH}")
  conan_basic_setup(TARGETS)

  if (ASIOCHAN_USE_STANDALONE_ASIO)
    target_link_libraries(asiochan INTERFACE CONAN_PKG::asio)
  else()
    target_link_libraries(asiochan INTERFACE CONAN_PKG::boost)
  endif()

  enable_testing()
  add_subdirectory(tests)
endif()
