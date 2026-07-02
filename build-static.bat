@echo off
setlocal enabledelayedexpansion
rem Single-file static release build. Statically links every dependency
rem (static CRT + static GLEW/SDL2/LASzip/E57Format/zstd) and embeds shaders +
rem icon, so the result is one self-contained exe with zero third-party DLLs.
rem See docs/decisions.md ("Single-File Static Release") for why the overlay
rem triplet in triplets\x64-windows-static.cmake exists (vcpkg/CMake-generator
rem MSVC toolset mismatch otherwise causes LNK2019 on __std_* STL symbols).
rem Builds into "build-static\" at the project root, regardless of the
rem directory this script is invoked from.
cd /d "%~dp0"

if not defined VCPKG_ROOT (
    if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=C:\vcpkg"
    ) else (
        echo [build-static.bat] ERROR: VCPKG_ROOT is not set and C:\vcpkg was not found.
        echo Set VCPKG_ROOT to your vcpkg checkout, e.g.:
        echo   set VCPKG_ROOT=C:\path\to\vcpkg
        exit /b 1
    )
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [build-static.bat] ERROR: %VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake not found.
    exit /b 1
)

echo [build-static.bat] Using vcpkg at %VCPKG_ROOT%
echo [build-static.bat] Configuring build-static\ (this recompiles static deps the first time - slow) ...
cmake -B build-static -S . -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_OVERLAY_TRIPLETS="%~dp0triplets"
if errorlevel 1 (
    echo [build-static.bat] CMake configure failed.
    exit /b 1
)

echo [build-static.bat] Building Release ...
cmake --build build-static --config Release
if errorlevel 1 (
    echo [build-static.bat] Build failed.
    echo If you see LNK2019 on __std_* symbols, the pinned MSVC toolset in
    echo triplets\x64-windows-static.cmake no longer matches this machine's
    echo default generator toolset - see docs\decisions.md for how to re-pin it.
    exit /b 1
)

echo [build-static.bat] Done. Single-file exe:
echo   build-static\Release\ViitorXPCViewer.exe
