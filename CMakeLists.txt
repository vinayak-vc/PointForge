cmake_minimum_required(VERSION 3.20)
project(ViitorXPC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PF_EMBED_WEB "Embed web UI assets into the executable" ON)
option(PF_REMOTE_WEBRTC "Enable WebRTC streaming support" ON)
option(PF_REMOTE_STREAM "Enable JPEG streaming fallback" ON)

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
find_package(civetweb CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)

if(PF_REMOTE_WEBRTC)
    find_package(LibDataChannel CONFIG QUIET)
    if(LibDataChannel_FOUND)
        set(PF_WEBRTC_LIB LibDataChannel::LibDataChannel)
    else()
        message(WARNING "PF_REMOTE_WEBRTC enabled but LibDataChannel not found; disabling.")
        set(PF_REMOTE_WEBRTC OFF)
    endif()
endif()

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
# PF_ZSTD_LIB/PF_ZSTD_INC are kept separate from PF_OPTIONAL_* because the Unity
# plugin (pfunity) needs zstd alone, without laszip/E57.
set(PF_ZSTD_LIB "")
set(PF_ZSTD_INC "")
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd)
    set(PF_ZSTD_LIB zstd::libzstd)
    message(STATUS "Zstd: target zstd::libzstd")
elseif(TARGET zstd::libzstd_static)
    set(PF_ZSTD_LIB zstd::libzstd_static)
    message(STATUS "Zstd: target zstd::libzstd_static")
else()
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
    find_library(ZSTD_LIB NAMES zstd libzstd HINTS "${_PF_REL_LIB}")
    if(ZSTD_INCLUDE_DIR AND ZSTD_LIB)
        set(PF_ZSTD_LIB ${ZSTD_LIB})
        set(PF_ZSTD_INC ${ZSTD_INCLUDE_DIR})
        message(STATUS "Zstd: ${ZSTD_LIB}")
    else()
        message(WARNING "Zstd not found -> Node compression disabled")
    endif()
endif()
if(PF_ZSTD_LIB)
    list(APPEND PF_OPTIONAL_INCS ${PF_ZSTD_INC})
    list(APPEND PF_OPTIONAL_LIBS ${PF_ZSTD_LIB})
    list(APPEND PF_OPTIONAL_DEFS PF_WITH_ZSTD)
endif()

message(STATUS "PointForge optional format libs: ${PF_OPTIONAL_LIBS}")
message(STATUS "PointForge format defines:       ${PF_OPTIONAL_DEFS}")

# ---------------------------------------------------------------------------
# Build version stamp — VERSION's patch number auto-increments every build;
# generates src/viewer/Version.h (PF_VERSION_STRING) consumed by main.cpp.
# ---------------------------------------------------------------------------
add_custom_target(version_header ALL
    COMMAND ${CMAKE_COMMAND}
            -DVERSION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/VERSION
            -DOUTPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/Version.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/bump_version.cmake
    COMMENT "Stamping build version"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Web UI embedding
# ---------------------------------------------------------------------------
if(PF_EMBED_WEB)
    add_custom_target(web_build ALL
        COMMAND ${CMAKE_COMMAND} -E echo "Installing npm packages..."
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/webremote npm.cmd install
        COMMAND ${CMAKE_COMMAND} -E echo "Building web UI..."
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR}/webremote npm.cmd run build
        BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/webremote/dist
        VERBATIM
    )
    # Deliberately an ALL custom_target (always executes), NOT an
    # OUTPUT-based add_custom_command keyed on `DEPENDS web_build`: a target
    # name isn't a file timestamp, so the VS generator was not reliably
    # detecting staleness and could relink pfview with a STALE
    # EmbeddedWeb.h even though webremote/dist itself had just been rebuilt
    # (observed live: EmbeddedWeb.h's mtime stuck ~1hr behind several
    # rebuilds). Same always-run pattern as web_build/version_header above.
    add_custom_target(embed_web_header ALL
        DEPENDS web_build
        COMMAND ${CMAKE_COMMAND} -DDIST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/webremote/dist -DOUTPUT_HEADER=${CMAKE_BINARY_DIR}/generated/EmbeddedWeb.h -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_web.cmake
        COMMENT "Generating EmbeddedWeb.h from webremote/dist"
        VERBATIM
    )
endif()

# ---------------------------------------------------------------------------
# Dear ImGui — compiled from source (pinned). The vcpkg imgui port dropped its
# SDL2 binding in 1.92.x, so we vendor ImGui + the SDL2/OpenGL3 backends here.
# This keeps the GUI layer independent of vcpkg's imgui feature churn.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking  # docking branch of the last series shipping the SDL2 backend
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
# pfunity — native plugin DLL for the Unity integration (branch library/unity).
# Streaming reader only: OctreeStore + Log + the flat C API. Deliberately does
# NOT link pfcore, so no laszip/E57/SDL/GL/ImGui dependencies leak into Unity.
# Build with the x64-windows-static triplet for a single self-contained DLL
# (static CRT + static zstd).
# ---------------------------------------------------------------------------
option(PF_BUILD_UNITY_PLUGIN "Build the PointForgeUnity native plugin DLL" ON)
if(PF_BUILD_UNITY_PLUGIN)
    add_library(pfunity SHARED
        src/library/unity/PointForgeC.cpp
        src/viewer/OctreeStore.cpp
        src/common/Log.cpp
    )
    target_include_directories(pfunity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pfunity PRIVATE glm::glm Threads::Threads)
    if(PF_ZSTD_LIB)
        target_link_libraries(pfunity PRIVATE ${PF_ZSTD_LIB})
        target_compile_definitions(pfunity PRIVATE PF_WITH_ZSTD)
        if(PF_ZSTD_INC)
            target_include_directories(pfunity PRIVATE ${PF_ZSTD_INC})
        endif()
    endif()
    set_target_properties(pfunity PROPERTIES OUTPUT_NAME "PointForgeUnity" PREFIX "")
endif()

# ---------------------------------------------------------------------------
# pfconvert — the importer CLI
# ---------------------------------------------------------------------------
add_executable(pfconvert src/tools/pfconvert/main.cpp)
target_link_libraries(pfconvert PRIVATE pfcore)

# ---------------------------------------------------------------------------
# pftest — synthetic .xyz round-trip test of the indexer (sequential vs
# parallel byte-identity + hierarchy invariants). Uses OctreeStore for the
# read-back; OctreeStore is GL-free (same pattern as pfunity).
# ---------------------------------------------------------------------------
option(PF_BUILD_TESTS "Build the pftest round-trip test executable" ON)
if(PF_BUILD_TESTS)
    add_executable(pftest
        src/tools/pftest/main.cpp
        src/viewer/OctreeStore.cpp
    )
    target_link_libraries(pftest PRIVATE pfcore Threads::Threads)
    if(PF_ZSTD_LIB)
        target_link_libraries(pftest PRIVATE ${PF_ZSTD_LIB})
        target_compile_definitions(pftest PRIVATE PF_WITH_ZSTD)
        if(PF_ZSTD_INC)
            target_include_directories(pftest PRIVATE ${PF_ZSTD_INC})
        endif()
    endif()
    enable_testing()
    add_test(NAME octree_roundtrip COMMAND pftest --points 500000)
endif()

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
    src/viewer/SerialController.cpp
    src/viewer/RemoteServer.cpp
    src/viewer/qrcodegen.cpp
    src/viewer/app.rc
)
target_link_libraries(pfview PRIVATE
    pfcore
    ${PF_SDL2_LIB} SDL2::SDL2main
    GLEW::GLEW
    OpenGL::GL
    imgui_local
    glm::glm
    Threads::Threads
    civetweb::civetweb
    nlohmann_json::nlohmann_json
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)
add_dependencies(pfview version_header)
if(WIN32)
    target_link_libraries(pfview PRIVATE ws2_32 iphlpapi)
endif()

if(PF_EMBED_WEB)
    add_dependencies(pfview embed_web_header)
    target_include_directories(pfview PRIVATE ${CMAKE_BINARY_DIR}/generated)
    target_compile_definitions(pfview PRIVATE PF_EMBED_WEB)
endif()
target_compile_definitions(pfview PRIVATE PF_WITH_REMOTE)
if(PF_REMOTE_WEBRTC)
    target_compile_definitions(pfview PRIVATE PF_REMOTE_WEBRTC)
    if(PF_WEBRTC_LIB)
        target_link_libraries(pfview PRIVATE ${PF_WEBRTC_LIB})
    endif()
endif()
if(PF_REMOTE_STREAM)
    target_compile_definitions(pfview PRIVATE PF_REMOTE_STREAM)
endif()

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

# Stamp the build version into the output filename, e.g.
# ViitorXPCViewer.exe -> ViitorXPCViewer_v102.exe for version 1.0.2. Safe to
# run every build: main.cpp includes the generated Version.h, so pfview
# always relinks (see tools/stamp_exe_name.cmake and decisions.md).
add_custom_command(TARGET pfview POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -DEXE_PATH=$<TARGET_FILE:pfview>
            -DVERSION_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/Version.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/stamp_exe_name.cmake
    VERBATIM
)
