# Overlay triplet: static single-binary build with the MSVC toolset pinned to
# the same version the project itself is compiled with (VS 2022 BuildTools,
# v14.44). Without the pin, vcpkg picks the newest toolset across ALL installed
# VS instances (e.g. 14.51 from VS 18 Community), and its newer STL objects
# reference __std_* helper symbols the 14.44 linker cannot resolve.
# Used via -DVCPKG_OVERLAY_TRIPLETS=<repo>/triplets (see CLAUDE.md).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)
