cmake_minimum_required(VERSION 3.16)
project(ClomDB VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(CLOMDB_BUILD_TESTS "Build ClomDB unit tests" ON)
option(CLOMDB_BUILD_CLI "Build the clomdb CLI tool" ON)

add_compile_options(-Wall -Wextra)

add_library(clomdb
  src/wal.cpp
  src/sstable.cpp
  src/version.cpp
  src/db.cpp
)
target_include_directories(clomdb PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
find_package(Threads REQUIRED)
target_link_libraries(clomdb PUBLIC Threads::Threads)

if(CLOMDB_BUILD_CLI)
  add_executable(clomdb_cli examples/clomdb_cli.cpp)
  target_link_libraries(clomdb_cli PRIVATE clomdb)

  add_executable(clomdb_bench examples/clomdb_bench.cpp)
  target_link_libraries(clomdb_bench PRIVATE clomdb)
endif()

if(CLOMDB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS clomdb ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
