cmake_minimum_required(VERSION 3.20)
project(PointForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /permissive- /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Detect a static vcpkg triplet (e.g. x64-windows-static). When set we link a
# single self-contained binary: static CRT + static GLEW. Guarded so the default
# dynamic triplet build is unaffected.
set(PF_STATIC OFF)
if(VCPKG_TARGET_TRIPLET MATCHES "static")
    set(PF_STATIC ON)
    message(STATUS "PointForge: STATIC build (${VCPKG_TARGET_TRIPLET}) -> single-binary mode")
    if(MSVC)
        # Match the static CRT (/MT) that x64-windows-static libraries use.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies (provided by vcpkg manifest; see vcpkg.json)
# ---------------------------------------------------------------------------
find_package(SDL2 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Static triplets export SDL2::SDL2-static rather than SDL2::SDL2. Pick whatever
# this triplet provides so the same CMakeLists serves both build modes.
if(TARGET SDL2::SDL2-static)
    set(PF_SDL2_LIB SDL2::SDL2-static)
else()
    set(PF_SDL2_LIB SDL2::SDL2)
endif()
message(STATUS "PointForge: SDL2 target = ${PF_SDL2_LIB}")

# ---------------------------------------------------------------------------
# Optional format libraries. We locate the header + import library directly
# (via CMAKE_PREFIX_PATH, which the vcpkg toolchain sets) instead of relying on
# a particular exported CMake target name, which varies across port versions.
# Each reader is guarded by a compile definition; the project still builds if a
# library is absent (that reader becomes a graceful stub).
# ---------------------------------------------------------------------------
set(PF_OPTIONAL_LIBS "")
set(PF_OPTIONAL_INCS "")
set(PF_OPTIONAL_DEFS "")

# --- LASzip (LAS/LAZ) ---
# vcpkg installs the header at <prefix>/include/laszip/laszip_api.h and the
# import lib as laszip3.lib, so we must search the laszip/ subdir and include
# the "laszip3" name. LASReader.cpp does #include <laszip_api.h>, which resolves
# because LASZIP_INCLUDE_DIR points straight at the laszip/ folder.
# Prefer the release static lib dir: vcpkg also exposes debug/lib, and a bare
# find_library can latch onto the debug variant -> CRT mismatch in a Release exe.
set(_PF_REL_LIB "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
find_path(LASZIP_INCLUDE_DIR NAMES laszip_api.h PATH_SUFFIXES laszip)
# Release-only first (NO_DEFAULT_PATH skips vcpkg's CMAKE_PREFIX_PATH, which also
# lists debug/lib and would otherwise win); fall back to a normal search.
find_library(LASZIP_LIB NAMES laszip3 laszip_api3 laszip_api laszip
             PATHS "${_PF_REL_LIB}" NO_DEFAULT_PATH)
if(NOT LASZIP_LIB)
    find_library(LASZIP_LIB NAMES laszip3 laszip_api3 laszip_api laszip)
endif()
if(LASZIP_INCLUDE_DIR AND LASZIP_LIB)
    list(APPEND PF_OPTIONAL_INCS ${LASZIP_INCLUDE_DIR})
    list(APPEND PF_OPTIONAL_LIBS ${LASZIP_LIB})
    list(APPEND PF_OPTIONAL_DEFS PF_WITH_LAS)
    message(STATUS "LASzip: ${LASZIP_LIB}")
else()
    message(WARNING "LASzip not found -> LAS/LAZ support disabled")
endif()

# --- libE57Format (E57) ---
find_package(E57Format CONFIG QUIET)
if(TARGET E57Format)
    list(APPEND PF_OPTIONAL_LIBS E57Format)
    list(APPEND PF_OPTIONAL_DEFS PF_WITH_E57)
    message(STATUS "libE57Format: target E57Format")
else()
    find_path(E57_INCLUDE_DIR NAMES E57SimpleReader.h PATH_SUFFIXES E57Format)
    find_library(E57_LIB NAMES E57Format)
    if(E57_INCLUDE_DIR AND E57_LIB)
        list(APPEND PF_OPTIONAL_INCS ${E57_INCLUDE_DIR})
        list(APPEND PF_OPTIONAL_LIBS ${E57_LIB})
        list(APPEND PF_OPTIONAL_DEFS PF_WITH_E57)
        message(STATUS "libE57Format: ${E57_LIB}")
    else()
        message(WARNING "libE57Format not found -> E57 support disabled")
    endif()
endif()

# --- Zstd (payload compression) ---
# Prefer the CMake config target (zstd::libzstd) so debug/release libs are chosen
# per-config automatically; fall back to a release-hinted find_library.
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd)
    list(APPEND PF_OPTIONAL_LIBS zstd::libzstd)
    list(APPEND PF_OPTIONAL_DEFS PF_WITH_ZSTD)
    message(STATUS "Zstd: target zstd::libzstd")
elseif(TARGET zstd::libzstd_static)
    list(APPEND PF_OPTIONAL_LIBS zstd::libzstd_static)
    list(APPEND PF_OPTIONAL_DEFS PF_WITH_ZSTD)
    message(STATUS "Zstd: target zstd::libzstd_static")
else()
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
    find_library(ZSTD_LIB NAMES zstd libzstd HINTS "${_PF_REL_LIB}")
    if(ZSTD_INCLUDE_DIR AND ZSTD_LIB)
        list(APPEND PF_OPTIONAL_INCS ${ZSTD_INCLUDE_DIR})
        list(APPEND PF_OPTIONAL_LIBS ${ZSTD_LIB})
        list(APPEND PF_OPTIONAL_DEFS PF_WITH_ZSTD)
        message(STATUS "Zstd: ${ZSTD_LIB}")
    else()
        message(WARNING "Zstd not found -> Node compression disabled")
    endif()
endif()

message(STATUS "PointForge optional format libs: ${PF_OPTIONAL_LIBS}")
message(STATUS "PointForge format defines:       ${PF_OPTIONAL_DEFS}")

# ---------------------------------------------------------------------------
# Dear ImGui — compiled from source (pinned). The vcpkg imgui port dropped its
# SDL2 binding in 1.92.x, so we vendor ImGui + the SDL2/OpenGL3 backends here.
# This keeps the GUI layer independent of vcpkg's imgui feature churn.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5     # last series shipping the SDL2 backend
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)   # imgui has no CMakeLists -> just populates

add_library(imgui_local STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_local PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# The SDL2 backend includes <SDL.h>; the OpenGL3 backend uses its own loader.
target_link_libraries(imgui_local PUBLIC ${PF_SDL2_LIB})

# ---------------------------------------------------------------------------
# Core static library shared by both executables
# ---------------------------------------------------------------------------
add_library(pfcore STATIC
    src/common/Log.cpp
    src/io/TextReader.cpp
    src/io/PLYReader.cpp
    src/io/LASReader.cpp
    src/io/E57Reader.cpp
    src/io/ReaderFactory.cpp
    src/indexer/Chunker.cpp
    src/indexer/OctreeIndexer.cpp
    src/indexer/MetadataWriter.cpp
    src/common/FileDialog.cpp
)
target_include_directories(pfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(pfcore PRIVATE ${PF_OPTIONAL_INCS})
target_link_libraries(pfcore PUBLIC glm::glm ${PF_OPTIONAL_LIBS} ole32)
target_compile_definitions(pfcore PUBLIC ${PF_OPTIONAL_DEFS})

# ---------------------------------------------------------------------------
# pfconvert — the importer CLI
# ---------------------------------------------------------------------------
add_executable(pfconvert src/tools/pfconvert/main.cpp)
target_link_libraries(pfconvert PRIVATE pfcore)

# ---------------------------------------------------------------------------
# pfview — the streaming viewer
# ---------------------------------------------------------------------------
add_executable(pfview WIN32
    src/viewer/main.cpp
    src/viewer/Camera.cpp
    src/viewer/Shader.cpp
    src/viewer/OctreeStore.cpp
    src/viewer/PointRenderer.cpp
    src/viewer/Controller.cpp
)
target_link_libraries(pfview PRIVATE
    pfcore
    ${PF_SDL2_LIB} SDL2::SDL2main
    GLEW::GLEW
    OpenGL::GL
    imgui_local
    glm::glm
    Threads::Threads
)

# Final release binary name.
set_target_properties(pfview PROPERTIES OUTPUT_NAME "ViitorXPCViewer")

if(PF_STATIC)
    # Static GLEW requires consumers to define GLEW_STATIC before <GL/glew.h>.
    target_compile_definitions(pfview PRIVATE GLEW_STATIC)
else()
    # Dynamic build only: copy shaders next to the binary (the static single-file
    # build embeds them via EmbeddedShaders.h, so no copy is needed there).
    add_custom_command(TARGET pfview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                $<TARGET_FILE_DIR:pfview>/shaders)
endif()
