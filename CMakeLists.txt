cmake_minimum_required(VERSION 3.16)
project(VibeOJ VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for IDE language server (clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# cpp-httplib
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# yaml-cpp
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

# Find system libraries
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# MySQL connector
find_library(MYSQL_CPPCONN_LIB NAMES mysqlcppconn mysqlcppconn8)
if(NOT MYSQL_CPPCONN_LIB)
  message(FATAL_ERROR "mysql-connector-cpp not found. Install libmysqlcppconn-dev")
endif()
message(STATUS "MySQL connector: ${MYSQL_CPPCONN_LIB}")

# Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  message(STATUS "GTest not found via find_package, will fetch from GitHub")
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  FetchContent_MakeAvailable(googletest)
endif()

# ── Main server executable ──────────────────────────────────
set(SOURCES
  src/main.cc
  src/common/log.cc
  src/config/config.cc
  src/config/seeder.cc
  src/db/connection_pool.cc
  src/db/dao.cc
  src/auth/jwt.cc
  src/auth/password.cc
  src/auth/middleware.cc
  src/handler/auth_handler.cc
  src/handler/problem_handler.cc
  src/handler/submit_handler.cc
  src/handler/admin_handler.cc
  src/judge/compiler.cc
  src/judge/sandbox.cc
  src/judge/runner.cc
)

add_executable(vibeoj-server ${SOURCES})

target_include_directories(vibeoj-server PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${json_SOURCE_DIR}/include
  ${yaml-cpp_SOURCE_DIR}/include
)

target_link_libraries(vibeoj-server PRIVATE
  httplib::httplib
  nlohmann_json::nlohmann_json
  yaml-cpp::yaml-cpp
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
  ${MYSQL_CPPCONN_LIB}
)

# ── Unit tests ──────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
  set(TEST_SOURCES
    tests/test_seeder.cc
    tests/test_log.cc
    tests/test_server.cc
    tests/test_db.cc
  )

  add_executable(vibeoj-tests ${TEST_SOURCES}
    src/config/seeder.cc
    src/config/config.cc
    src/common/log.cc
    src/db/connection_pool.cc
    src/db/dao.cc
  )

  target_include_directories(vibeoj-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${yaml-cpp_SOURCE_DIR}/include
  )

  target_link_libraries(vibeoj-tests PRIVATE
    httplib::httplib
    yaml-cpp::yaml-cpp
    Threads::Threads
    ${MYSQL_CPPCONN_LIB}
  )

  # Link GTest (either system or fetched)
  if(GTest_FOUND)
    target_link_libraries(vibeoj-tests PRIVATE GTest::GTest GTest::Main)
  else()
    target_link_libraries(vibeoj-tests PRIVATE gtest gtest_main)
  endif()

  # Register CTest
  enable_testing()
  add_test(NAME vibeoj-unit-tests COMMAND vibeoj-tests)

  message(STATUS "Unit tests: enabled")
endif()
