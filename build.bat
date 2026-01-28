@echo off
REM Create necessary directories
if not exist assets\maps mkdir assets\maps
if not exist assets\tokens mkdir assets\tokens
if not exist saves mkdir saves

REM Compile STB libraries only if needed (much faster subsequent builds)
if not exist stb_impl.o (
    echo Compiling STB libraries...
    C:\msys64\mingw64\bin\gcc.exe -c -O2 -std=c11 stb_impl.c -o stb_impl.o
    if %errorlevel% neq 0 (
        echo STB compilation failed!
        exit /b 1
    )
)

REM Compile main.c and link with STB
echo Compiling main.c...
C:\msys64\mingw64\bin\gcc.exe -Wall -Wextra -O2 -std=c11 main.c stb_impl.o -o vtt.exe -IC:/msys64/mingw64/include/SDL3 -LC:/msys64/mingw64/lib -lSDL3 -lm

if %errorlevel% equ 0 (
    echo Build successful!
    echo.
    echo Copying SDL3.dll for distribution...
    copy C:\msys64\mingw64\bin\SDL3.dll . >nul 2>&1
    if exist SDL3.dll (
        echo SDL3.dll copied - ready for GitHub release
    )
) else (
    echo Build failed!
    exit /b 1
)