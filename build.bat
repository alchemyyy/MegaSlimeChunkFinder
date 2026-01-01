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

REM Setup Visual Studio environment (suppress verbose output)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

echo.
echo Compiling main program with AVX-512 optimizations...
echo.

REM Compile main program with maximum optimizations
cl /std:c++17 /O2 /Oi /Ot /GL /arch:AVX512 /favor:INTEL64 /EHsc /nologo /Fe:megaslimechunkfinder.exe megaslimechunkfinder.cpp slimechunk_impl.cpp /link /LTCG

set MAIN_SUCCESS=%ERRORLEVEL%

echo.
echo Compiling test program...
echo.

REM Compile test program
cl /std:c++17 /O2 /Oi /Ot /GL /arch:AVX512 /favor:INTEL64 /EHsc /nologo /Fe:test_slimechunk.exe test_slimechunk.cpp slimechunk_impl.cpp /link /LTCG

set TEST_SUCCESS=%ERRORLEVEL%

echo.
echo ========================================
if %MAIN_SUCCESS% EQU 0 (
    echo Main program: SUCCESSFUL
    echo   Executable: megaslimechunkfinder.exe
) else (
    echo Main program: FAILED
)

if %TEST_SUCCESS% EQU 0 (
    echo Test program: SUCCESSFUL
    echo   Executable: test_slimechunk.exe
) else (
    echo Test program: FAILED
)
echo ========================================
echo.

if %MAIN_SUCCESS% EQU 0 (
    echo To run main program: megaslimechunkfinder.exe
    echo Press Ctrl+C while running to see stats
    echo.
)

if %TEST_SUCCESS% EQU 0 (
    echo To run tests: test_slimechunk.exe
    echo.
)

pause
