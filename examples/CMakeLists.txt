add_executable(asiochan_example_sum)
target_link_libraries(
  asiochan_example_sum

  PRIVATE
  Threads::Threads
  asiochan::asiochan
)
target_sources(
  asiochan_example_sum

  PRIVATE
  example_sum.cpp
)
add_executable(asiochan_playground)
target_link_libraries(
  asiochan_playground

  PRIVATE
  Threads::Threads
  asiochan::asiochan
)
target_sources(
  asiochan_playground

  PRIVATE
  playground.cpp
)
