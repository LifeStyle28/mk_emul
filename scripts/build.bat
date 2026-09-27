@echo off
rem Configure and build mk_emul (Release) with MSVC + Ninja.
rem   build.bat        build
rem   build.bat test   build, then run all tests (CTest)
rem Override paths with environment variables QT_DIR / VCVARS if they differ.
setlocal

if not defined QT_DIR set "QT_DIR=C:/Qt/6.8.3/msvc2022_64"
if not defined VCVARS (
    for %%P in (
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) do if not defined VCVARS if exist %%P set "VCVARS=%%~P"
)
if not defined VCVARS (
    echo vcvars64.bat not found. Install Visual Studio with "Desktop development with C++".
    exit /b 1
)

rem Localized MSVC prints "Note: including file:" translated, in the console code page. CMake
rem stores that prefix for Ninja as UTF-8, so with an OEM code page Ninja cannot match it, header
rem dependencies are lost and stale objects get linked. Use UTF-8 (and English if installed).
chcp 65001 >nul
set VSLANG=1033
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0.."
if not exist build\build.ninja (
    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=%QT_DIR% -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build || exit /b 1
echo.
echo Built: %CD%\build\mk_emul.exe
if /i "%~1"=="test" (
    ctest --test-dir build --output-on-failure || exit /b 1
)
