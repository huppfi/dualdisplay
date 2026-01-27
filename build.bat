@echo off
REM Create necessary directories
if not exist assets\maps mkdir assets\maps
if not exist assets\tokens mkdir assets\tokens
if not exist saves mkdir saves

REM Compile the single file (using stb_image.h, no SDL_image needed)
gcc -Wall -Wextra -g -std=c11 main.c -o vtt.exe -I/c/msys64/mingw64/include/SDL3 -L/c/msys64/mingw64/lib -lSDL3 -lm

if %errorlevel% equ 0 (
    echo Build successful! Run with: vtt.exe
) else (
    echo Build failed!
    exit /b 1
)
