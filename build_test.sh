#!/bin/bash
# Build AVX-512 test

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
VSPATH=$("$VSWHERE" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)

# Find MSVC version
MSVCVER=$(ls "$VSPATH/VC/Tools/MSVC" | sort -V | tail -1)

COMPILER="$VSPATH/VC/Tools/MSVC/$MSVCVER/bin/Hostx64/x64/cl.exe"

# Find Windows SDK
SDKROOT=$(reg query "HKLM\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots" //v KitsRoot10 2>/dev/null | grep KitsRoot10 | awk '{print $3}')
SDKVER=$(ls "$SDKROOT/Include" | grep '^10\.' | sort -V | tail -1)

# Build include and lib paths
INC1="$VSPATH/VC/Tools/MSVC/$MSVCVER/include"
INC2="$SDKROOT/Include/$SDKVER/ucrt"
INC3="$SDKROOT/Include/$SDKVER/um"
INC4="$SDKROOT/Include/$SDKVER/shared"

LIB1="$VSPATH/VC/Tools/MSVC/$MSVCVER/lib/x64"
LIB2="$SDKROOT/Lib/$SDKVER/ucrt/x64"
LIB3="$SDKROOT/Lib/$SDKVER/um/x64"

echo "Building AVX-512 test..."
"$COMPILER" /std:c++17 /O2 /arch:AVX512 /EHsc /nologo \
  /I"$INC1" /I"$INC2" /I"$INC3" /I"$INC4" \
  avx512_test.cpp \
  /link /LIBPATH:"$LIB1" /LIBPATH:"$LIB2" /LIBPATH:"$LIB3" \
  /OUT:avx512_test.exe

if [ $? -eq 0 ]; then
    echo "Build successful! Running test..."
    ./avx512_test.exe
else
    echo "Build failed!"
    exit 1
fi
