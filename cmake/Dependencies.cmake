# ────────────────────── Melody Matrix Dependencies ──────────────────
# Vendored approach: all dependencies are expected in third_party/
# Use scripts/download_deps.py to fetch them.
#
# Expected directory layout:
#   third_party/SDL2-2.30.8/   (pre-built SDL2 for MSVC)
#   third_party/glm/            (header-only, from GitHub)
#   third_party/miniaudio/      (header-only, from GitHub)
#   third_party/glad/           (generated, from GitHub glad2)
#   third_party/imgui/          (from GitHub docking branch)
#   third_party/Catch2/         (for unit tests, from GitHub)

# ═══════════════════════════════════════════════════════════════════
# SDL2 — pre-built development libraries for MSVC
# ═══════════════════════════════════════════════════════════════════
set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.8")

if(EXISTS "${SDL2_DIR}/cmake")
    # Use the CMake config that ships with SDL2 devel package
    set(SDL2_ROOT "${SDL2_DIR}")
    find_package(SDL2 REQUIRED NO_DEFAULT_PATH
        PATHS "${SDL2_DIR}"
        NO_CMAKE_FIND_ROOT_PATH
    )
    # The SDL2 devel package provides SDL2::SDL2 and SDL2::SDL2main
    if(NOT TARGET SDL2::SDL2)
        # Manual target creation fallback
        add_library(SDL2::SDL2 STATIC IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${SDL2_DIR}/lib/x64/SDL2.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_DIR}/include"
        )
        add_library(SDL2::SDL2main STATIC IMPORTED)
        set_target_properties(SDL2::SDL2main PROPERTIES
            IMPORTED_LOCATION "${SDL2_DIR}/lib/x64/SDL2main.lib"
        )
    endif()
else()
    # Try system-installed SDL2 as fallback
    find_package(SDL2 REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════
# GLM — header-only math library
# ═══════════════════════════════════════════════════════════════════
set(GLM_DIR "${CMAKE_SOURCE_DIR}/third_party/glm")
if(EXISTS "${GLM_DIR}/CMakeLists.txt")
    set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${GLM_DIR}" ${CMAKE_BINARY_DIR}/_deps/glm-build)
elseif(EXISTS "${GLM_DIR}/glm")
    add_library(glm::glm INTERFACE IMPORTED)
    set_target_properties(glm::glm PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${GLM_DIR}"
    )
else()
    find_package(glm REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════
# GLAD — OpenGL 3.3 Core loader (glad2)
# ═══════════════════════════════════════════════════════════════════
set(GLAD_DIR "${CMAKE_SOURCE_DIR}/third_party/glad")
if(EXISTS "${GLAD_DIR}/CMakeLists.txt")
    # glad2 with its CMake system
    glad_add_library(glad_gl33_core STATIC API gl:core=3.3)
else()
    # Fallback: generate glad files locally
    message(STATUS "GLAD not found in third_party/. Generating minimal GL loader...")
    set(GLAD_GEN_DIR "${CMAKE_BINARY_DIR}/generated/glad")
    file(MAKE_DIRECTORY "${GLAD_GEN_DIR}")

    # Write minimal glad header that includes system OpenGL headers
    # This is a placeholder — proper glad generation requires Python glad2
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/glad_placeholder.h.in"
        "${GLAD_GEN_DIR}/glad.h"
    )
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/glad_placeholder.c.in"
        "${GLAD_GEN_DIR}/glad.c"
    )
    add_library(glad_gl33_core STATIC "${GLAD_GEN_DIR}/glad.c")
    target_include_directories(glad_gl33_core PUBLIC "${GLAD_GEN_DIR}")
    target_link_libraries(glad_gl33_core PUBLIC opengl32)
endif()

# ═══════════════════════════════════════════════════════════════════
# miniaudio — single-header audio library
# ═══════════════════════════════════════════════════════════════════
set(MINIAUDIO_DIR "${CMAKE_SOURCE_DIR}/third_party/miniaudio")
if(EXISTS "${MINIAUDIO_DIR}/miniaudio.h")
    add_library(miniaudio::miniaudio INTERFACE IMPORTED)
    set_target_properties(miniaudio::miniaudio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MINIAUDIO_DIR}"
    )
else()
    find_path(MINIAUDIO_INCLUDE_DIR "miniaudio.h")
    if(MINIAUDIO_INCLUDE_DIR)
        add_library(miniaudio::miniaudio INTERFACE IMPORTED)
        set_target_properties(miniaudio::miniaudio PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${MINIAUDIO_INCLUDE_DIR}"
        )
    else()
        message(WARNING "miniaudio not found. Audio subsystem will be disabled.")
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════
# Dear ImGui — immediate mode GUI
# ═══════════════════════════════════════════════════════════════════
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
    )
    # Add backends if they exist
    if(EXISTS "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp")
        target_sources(imgui PRIVATE
            ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
            ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(imgui PUBLIC "${IMGUI_DIR}/backends")
    endif()
    target_include_directories(imgui PUBLIC "${IMGUI_DIR}")
    target_link_libraries(imgui PUBLIC SDL2::SDL2)
else()
    message(WARNING "Dear ImGui not found in third_party/. UI will be disabled.")
    add_library(imgui INTERFACE)
endif()

# ═══════════════════════════════════════════════════════════════════
# Catch2 (unit tests only)
# ═══════════════════════════════════════════════════════════════════
if(MM_BUILD_TESTS)
    set(CATCH2_DIR "${CMAKE_SOURCE_DIR}/third_party/Catch2")
    if(EXISTS "${CATCH2_DIR}/CMakeLists.txt")
        add_subdirectory("${CATCH2_DIR}" ${CMAKE_BINARY_DIR}/_deps/catch2-build)
    else()
        find_package(Catch2 3 REQUIRED)
    endif()
endif()
