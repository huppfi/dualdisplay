#!/bin/bash

# Create necessary directories
mkdir -p assets/maps assets/tokens saves

# Compile the single file (using stb_image.h, no SDL_image needed)
gcc -Wall -Wextra -g -std=c11 \
    main.c \
    -o vtt \
    $(pkg-config --cflags --libs sdl3) \
    -lm

# Check if compilation succeeded
if [ $? -eq 0 ]; then
    echo "Build successful! Run with: ./vtt"
else
    echo "Build failed!"
    exit 1
fi
