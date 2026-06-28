@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set CMAKE_EXE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA_EXE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
cd /d D:\colin\Melody-Matrix
if exist build-ninja rmdir /s /q build-ninja
mkdir build-ninja
cd build-ninja
%CMAKE_EXE% -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM=%NINJA_EXE% ..
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
%NINJA_EXE%
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD SUCCESS
