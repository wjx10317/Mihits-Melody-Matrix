cmake_minimum_required(VERSION 3.20)
project(melody_matrix VERSION 0.1.0 LANGUAGES CXX C)

# ────────────────────── C++17, no extensions ──────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ────────────────────── Version macros ────────────────────────────
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
)

# ────────────────────── Output directories ───────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ────────────────────── Warnings ─────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /FS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ────────────────────── Dependencies ─────────────────────────────
include("${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake")

# ────────────────────── Main executable ──────────────────────────
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.h")
list(FILTER SOURCES EXCLUDE REGEX "src/version\\.h\\.in$")

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/stb"
)

# SDL2 include: the devel package has headers in include/ but our code
# uses #include <SDL2/SDL.h>, so add the parent of include/ as a path
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.8/include/SDL.h")
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.8/include"
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    glm::glm
    imgui
    miniaudio::miniaudio
    glad_gl33_core
)

# Windows-specific: link Comdlg32 for native file dialogs (GetOpenFileName)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE Comdlg32 ole32 uuid)
endif()

# Post-build: copy SDL2 DLL
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.8/lib/x64/SDL2.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.8/lib/x64/SDL2.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/SDL2.dll"
        COMMENT "Copying SDL2.dll"
    )
endif()

# ────────────────────── Post-build: copy assets ──────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
    COMMENT "Copying assets to output directory"
)

# ────────────────────── Post-build: copy res ───────────────────
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/res"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/res"
    COMMENT "Copying res to output directory"
)

# ────────────────────── Tests (optional) ─────────────────────────
option(MM_BUILD_TESTS "Build unit tests" OFF)
if(MM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
