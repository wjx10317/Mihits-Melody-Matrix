# Melody Matrix Unit Tests

find_package(Catch2 3 REQUIRED)

# Collect test sources
file(GLOB_RECURSE TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(mm_tests ${TEST_SOURCES})

target_include_directories(mm_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(mm_tests PRIVATE
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(mm_tests)
