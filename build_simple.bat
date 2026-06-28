@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
cd /d D:\colin\Melody-Matrix
if not exist build-vs mkdir build-vs
cd build-vs
"%CMAKE_EXE%" -G "Visual Studio 17 2022" -A x64 .. 2>&1
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
"%CMAKE_EXE%" --build . --config Debug 2>&1
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD SUCCESS
