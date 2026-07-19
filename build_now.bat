@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo Failed to set up VS 2022 vcvars64. Is Visual Studio installed?
    exit /b 1
)

set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%CMAKE_EXE%" (
    echo CMake not found at expected VS path.
    exit /b 1
)

if exist build-ninja rmdir /s /q build-ninja
mkdir build-ninja
cd build-ninja
"%CMAKE_EXE%" -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ..
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
"%NINJA_EXE%"
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD SUCCESS
echo Run: "%~dp0build-ninja\melody_matrix.exe"
endlocal
