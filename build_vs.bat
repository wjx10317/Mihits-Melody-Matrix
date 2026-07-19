@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo Failed to set up VS 2022 vcvars64. Is Visual Studio installed?
    exit /b 1
)

set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    echo CMake not found at expected VS path.
    exit /b 1
)

if exist build-vs rmdir /s /q build-vs
mkdir build-vs
cd build-vs
"%CMAKE_EXE%" -G "Visual Studio 17 2022" -A x64 ..
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
"%CMAKE_EXE%" --build . --config Debug
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD SUCCESS
echo Run: "%~dp0build-vs\bin\Debug\melody_matrix.exe"
endlocal
