# embed_web.cmake - Generates EmbeddedWeb.h from webremote/dist
# This script reads all files in ${CMAKE_CURRENT_SOURCE_DIR}/webremote/dist
# and creates a C++ header with byte arrays.

if(NOT DEFINED DIST_DIR)
    set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webremote/dist")
endif()
if(NOT DEFINED OUTPUT_HEADER)
    set(OUTPUT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/viewer/EmbeddedWeb.h")
endif()

file(WRITE ${OUTPUT_HEADER} "// Auto-generated EmbeddedWeb.h\n#pragma once\n\n#include <map>\n#include <string>\n#include <utility>\n\n")

file(GLOB_RECURSE WEB_FILES "${DIST_DIR}/*")
foreach(FILEPATH ${WEB_FILES})
    file(RELATIVE_PATH RELPATH "${DIST_DIR}" "${FILEPATH}")
    string(REPLACE "\\" "/" RELPATH "${RELPATH}")
    # Convert to C array (hex representation)
    file(READ ${FILEPATH} FILE_CONTENT HEX)
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" [[0x\1, ]] HEX_ARRAY "${FILE_CONTENT}")
    set(VARNAME "web_${RELPATH}")
    string(REGEX REPLACE "[^a-zA-Z0-9]" "_" VARNAME "${VARNAME}")
    file(APPEND ${OUTPUT_HEADER} "static const unsigned char ${VARNAME}[] = { ${HEX_ARRAY} } ;\n")
    file(APPEND ${OUTPUT_HEADER} "static const size_t ${VARNAME}_len = sizeof(${VARNAME});\n\n")
endforeach()

# Generate lookup map function
file(APPEND ${OUTPUT_HEADER} "static std::map<std::string, std::pair<const unsigned char*, size_t>> getEmbeddedWebFiles() {\n    return {\n")
foreach(FILEPATH ${WEB_FILES})
    file(RELATIVE_PATH RELPATH "${DIST_DIR}" "${FILEPATH}")
    string(REPLACE "\\" "/" RELPATH "${RELPATH}")
    set(VARNAME "web_${RELPATH}")
    string(REGEX REPLACE "[^a-zA-Z0-9]" "_" VARNAME "${VARNAME}")
    file(APPEND ${OUTPUT_HEADER} "        {\"/${RELPATH}\", {${VARNAME}, ${VARNAME}_len}},\n")
endforeach()
file(APPEND ${OUTPUT_HEADER} "    };\n}\n")
