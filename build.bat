@echo off
echo ========================================
echo Slime Chunk Finder - Compilation Script
echo ========================================
echo.

REM Find Visual Studio installation
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio installer not found!
    echo Please install Visual Studio 2022 with C++ tools.
    pause
    exit /b 1
)

REM Get VS installation path
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSPATH=%%i"
)

if not defined VSPATH (
    echo ERROR: Visual Studio C++ tools not found!
    echo Please install Visual Studio 2022 with "Desktop development with C++" workload.
    pause
    exit /b 1
)

echo Found Visual Studio at: %VSPATH%
echo.

REM Setup Visual Studio environment
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo Compiling with AVX-512 optimizations...
echo.

REM Compile with maximum optimizations
cl /std:c++17 /O2 /Oi /Ot /GL /arch:AVX512 /favor:INTEL64 /EHsc /nologo /Fe:megaslimechunkfinder.exe megaslimechunkfinder.cpp /link /LTCG

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Compilation SUCCESSFUL!
    echo ========================================
    echo.
    echo Executable: megaslimechunkfinder.exe
    echo.
    echo To run: megaslimechunkfinder.exe
    echo Press Ctrl+C while running to see stats
    echo.
) else (
    echo.
    echo ========================================
    echo Compilation FAILED!
    echo ========================================
    echo.
)

pause
