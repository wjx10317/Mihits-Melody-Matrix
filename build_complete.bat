@echo off
REM Melody Matrix 完整构建脚本
REM 自动设置环境并编译

setlocal EnableDelayedExpansion

echo ========================================
echo   Melody Matrix 构建脚本
echo ========================================
echo.

REM 设置项目目录
set PROJECT_DIR=D:\colin\Melody-Matrix
cd /d %PROJECT_DIR%

REM ========================================
REM 步骤1：设置 MSVC 环境
REM ========================================
echo [1/4] 设置 MSVC 环境...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo   [ERROR] 无法设置 MSVC 环境
    goto :error
)
echo   [OK] MSVC 环境已设置
echo.

REM ========================================
REM 步骤2：配置 CMake
REM ========================================
echo [2/4] 配置 CMake...

REM 清理旧的构建目录
if exist "build-ninja" (
    echo   - 清理旧构建目录...
    rmdir /s /q "build-ninja" 2>nul
)
mkdir "build-ninja" 2>nul
cd "build-ninja"

REM 运行 CMake
echo   - 运行 CMake（生成 Ninja 构建文件）...
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ..
if errorlevel 1 (
    echo   [ERROR] CMake 配置失败
    goto :error
)
echo   [OK] CMake 配置完成
echo.

REM ========================================
REM 步骤3：编译项目
REM ========================================
echo [3/4] 编译项目（这可能需要几分钟）...
echo.

ninja
if errorlevel 1 (
    echo.
    echo   [ERROR] 编译失败！
    goto :error
)

echo.
echo   [OK] 编译成功！
echo.

REM ========================================
REM 步骤4：复制资源文件
REM ========================================
echo [4/4] 复制资源文件...
cd /d %PROJECT_DIR%
if not exist "build-ninja\assets" mkdir "build-ninja\assets" 2>nul
xcopy /E /I /Y "assets" "build-ninja\assets" >nul 2>&1
if not exist "build-ninja\res" mkdir "build-ninja\res" 2>nul
xcopy /E /I /Y "res" "build-ninja\res" >nul 2>&1
echo   [OK] 资源文件已复制
echo.

REM ========================================
REM 完成
REM ========================================
echo ========================================
echo   构建完成！
echo ========================================
echo.
echo 输出文件：
echo   D:\colin\Melody-Matrix\build-ninja\melody_matrix.exe
echo.
echo 运行游戏：
echo   cd D:\colin\Melody-Matrix\build-ninja
echo   melody_matrix.exe
echo.
goto :end

:error
echo.
echo ========================================
echo   构建失败！
echo ========================================
echo.
echo 请查看上面的错误信息
echo.
pause
exit /b 1

:end
endlocal
pause
