@echo off
setlocal enabledelayedexpansion
rem Dynamic build (fast iteration). Always builds into "build\" at the project
rem root, regardless of the directory this script is invoked from.
cd /d "%~dp0"

if not defined VCPKG_ROOT (
    if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=C:\vcpkg"
    ) else (
        echo [build.bat] ERROR: VCPKG_ROOT is not set and C:\vcpkg was not found.
        echo Set VCPKG_ROOT to your vcpkg checkout, e.g.:
        echo   set VCPKG_ROOT=C:\path\to\vcpkg
        exit /b 1
    )
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [build.bat] ERROR: %VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake not found.
    exit /b 1
)

echo [build.bat] Using vcpkg at %VCPKG_ROOT%
echo [build.bat] Configuring build\ ...
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 (
    echo [build.bat] CMake configure failed.
    exit /b 1
)

echo [build.bat] Building Release ...
cmake --build build --config Release
if errorlevel 1 (
    echo [build.bat] Build failed.
    exit /b 1
)

echo [build.bat] Done. Binaries in build\Release\
echo   build\Release\pfconvert.exe
echo   build\Release\ViitorXPCViewer.exe
