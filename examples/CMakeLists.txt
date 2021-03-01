add_executable(asiochan_example_sum)
target_link_libraries(
  asiochan_example_sum

  PRIVATE
  Threads::Threads
  asiochan::asiochan
#  -fsanitize=address
)
#target_compile_options(asiochan_example_sum PRIVATE
#  -fsanitize=address)
target_sources(
  asiochan_example_sum

  PRIVATE
  example_sum.cpp
)