# stamp_exe_name.cmake - Renames the freshly-linked pfview exe to embed the
# build version (e.g. ViitorXPCViewer.exe -> ViitorXPCViewer_v102.exe for
# 1.0.2), so a tester can tell two builds apart just from the filename.
#
# Runs as a POST_BUILD step; safe to run on every build because main.cpp
# includes the generated Version.h, so its changing content forces a
# recompile + relink of pfview (and therefore a fresh unversioned exe to
# rename) on every single build invocation — see decisions.md.

if(NOT DEFINED EXE_PATH)
    message(FATAL_ERROR "stamp_exe_name.cmake requires -DEXE_PATH=...")
endif()
if(NOT DEFINED VERSION_HEADER)
    message(FATAL_ERROR "stamp_exe_name.cmake requires -DVERSION_HEADER=...")
endif()

if(NOT EXISTS "${EXE_PATH}")
    message(WARNING "stamp_exe_name.cmake: ${EXE_PATH} not found, skipping rename")
    return()
endif()

file(READ "${VERSION_HEADER}" PF_VERSION_HEADER_CONTENT)
string(REGEX MATCH "PF_VERSION_MAJOR ([0-9]+)" _ "${PF_VERSION_HEADER_CONTENT}")
set(PF_MAJ "${CMAKE_MATCH_1}")
string(REGEX MATCH "PF_VERSION_MINOR ([0-9]+)" _ "${PF_VERSION_HEADER_CONTENT}")
set(PF_MIN "${CMAKE_MATCH_1}")
string(REGEX MATCH "PF_VERSION_PATCH ([0-9]+)" _ "${PF_VERSION_HEADER_CONTENT}")
set(PF_PAT "${CMAKE_MATCH_1}")

if(PF_MAJ STREQUAL "" OR PF_MIN STREQUAL "" OR PF_PAT STREQUAL "")
    message(WARNING "stamp_exe_name.cmake: could not parse version from ${VERSION_HEADER}, skipping rename")
    return()
endif()

get_filename_component(EXE_DIR "${EXE_PATH}" DIRECTORY)
get_filename_component(EXE_STEM "${EXE_PATH}" NAME_WE)
get_filename_component(EXE_EXT  "${EXE_PATH}" EXT)

# Drop any stale versioned copy from a previous build so old builds don't pile up.
file(GLOB PF_OLD_VERSIONED "${EXE_DIR}/${EXE_STEM}_v*${EXE_EXT}")
foreach(PF_OLD ${PF_OLD_VERSIONED})
    file(REMOVE "${PF_OLD}")
endforeach()

set(PF_NEW_NAME "${EXE_STEM}_v${PF_MAJ}${PF_MIN}${PF_PAT}${EXE_EXT}")
file(RENAME "${EXE_PATH}" "${EXE_DIR}/${PF_NEW_NAME}")
message(STATUS "Renamed exe: ${EXE_STEM}${EXE_EXT} -> ${PF_NEW_NAME}")
