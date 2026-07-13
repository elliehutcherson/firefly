add_library(firefly_lib greeting.cc)
target_include_directories(firefly_lib PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(firefly_lib PUBLIC absl::str_format)

add_executable(firefly main.cc)
target_link_libraries(firefly PRIVATE firefly_lib)
