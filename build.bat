@echo off
REM Melody Matrix — 唯一构建入口（VS 2022 + CMake）
setlocal
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 无法设置 MSVC 环境，请安装 Visual Studio 2022（含 C++ 与 CMake）。
    exit /b 1
)

set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    echo [ERROR] 未找到 CMake: "%CMAKE_EXE%"
    exit /b 1
)

if not exist build-vs mkdir build-vs
cd build-vs

echo [1/2] CMake configure...
"%CMAKE_EXE%" -G "Visual Studio 17 2022" -A x64 ..
if errorlevel 1 (
    echo [ERROR] CMake 配置失败
    exit /b 1
)

echo [2/2] Build Debug...
"%CMAKE_EXE%" --build . --config Debug --target melody_matrix -j 8
if errorlevel 1 (
    echo [ERROR] 编译失败
    exit /b 1
)

echo.
echo BUILD SUCCESS
echo 运行: "%~dp0build-vs\bin\Debug\melody_matrix.exe"
echo 说明: 仓库不含谱面，请在游戏内导入 .osz
endlocal
