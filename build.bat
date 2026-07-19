@echo off
REM Melody Matrix - single build entry (VS 2022 + CMake)
setlocal EnableExtensions
cd /d "%~dp0"
set "ROOT=%CD%"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] VS 2022 vcvars64.bat not found.
    echo Install Visual Studio 2022 with Desktop development with C++.
    exit /b 1
)
call "%VCVARS%"
if errorlevel 1 (
    echo [ERROR] Failed to initialize MSVC environment.
    exit /b 1
)

set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    echo [ERROR] CMake not found:
    echo   %CMAKE_EXE%
    exit /b 1
)

if not exist "%ROOT%\build-vs" mkdir "%ROOT%\build-vs"
pushd "%ROOT%\build-vs"

echo [1/3] CMake configure...
"%CMAKE_EXE%" -G "Visual Studio 17 2022" -A x64 "%ROOT%"
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    popd
    exit /b 1
)

echo [2/3] Build Debug...
"%CMAKE_EXE%" --build . --config Debug --target melody_matrix --parallel 8
if errorlevel 1 (
    echo [ERROR] Build failed.
    popd
    exit /b 1
)

set "OUT=%ROOT%\build-vs\bin\Debug"
if not exist "%OUT%\melody_matrix.exe" (
    echo [ERROR] Missing output: "%OUT%\melody_matrix.exe"
    popd
    exit /b 1
)
if not exist "%OUT%\SDL2.dll" (
    echo [ERROR] Missing output: "%OUT%\SDL2.dll"
    popd
    exit /b 1
)

echo [3/3] Copy exe and SDL2.dll to repo root...
copy /Y "%OUT%\melody_matrix.exe" "%ROOT%\melody_matrix.exe" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy melody_matrix.exe
    popd
    exit /b 1
)
copy /Y "%OUT%\SDL2.dll" "%ROOT%\SDL2.dll" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy SDL2.dll
    popd
    exit /b 1
)

popd
echo.
echo BUILD SUCCESS
echo Run: "%ROOT%\melody_matrix.exe"
echo Note: beatmaps are not in repo; import .osz in-game.
endlocal
exit /b 0
