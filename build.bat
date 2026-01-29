@echo off
REM Create necessary directories
if not exist assets\maps mkdir assets\maps
if not exist assets\tokens mkdir assets\tokens
if not exist saves mkdir saves

REM Optional: Embed font in executable (run once if you have font.ttf)
REM Uncomment to enable:
REM if exist font.ttf (
REM     python embed_font.py font.ttf > font_embedded.h
REM     echo Font embedded - compiling with -DEMBED_FONT
REM     set EMBED=-DEMBED_FONT
REM ) else (
REM     set EMBED=
REM )

REM Compile STB libraries only if needed (much faster subsequent builds)
if not exist stb_impl.o (
    echo Compiling STB libraries...
    C:\msys64\mingw64\bin\gcc.exe -c -O2 -std=c11 stb_impl.c -o stb_impl.o
    if %errorlevel% neq 0 (
        echo STB compilation failed!
        exit /b 1
    )
)

REM Compile main.c and link with STB (static linking for portability)
echo Compiling main.c...
C:\msys64\mingw64\bin\gcc.exe -Wall -Wextra -O2 -std=c11 %EMBED% main.c stb_impl.o -o vtt.exe -IC:/msys64/mingw64/include/SDL3 -LC:/msys64/mingw64/lib -lSDL3 -lm -static-libgcc -static-libstdc++

if %errorlevel% equ 0 (
    echo Build successful!
    echo.
    echo Copying SDL3.dll for distribution...
    copy C:\msys64\mingw64\bin\SDL3.dll . >nul 2>&1
    if exist SDL3.dll (
        echo SDL3.dll copied
    )
    echo.
    echo Distribution package should include:
    echo   - vtt.exe
    echo   - SDL3.dll
    echo   - font.ttf ^(optional - will use system fonts if missing^)
    echo   - assets/ folder ^(for maps and tokens^)
    echo   - saves/ folder ^(for save files^)
) else (
    echo Build failed!
    exit /b 1
)