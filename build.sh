#!/bin/bash
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to check if running on Arch Linux
is_arch() {
    [ -f /etc/arch-release ]
}

# Function to check if running on Steam Deck
is_steamdeck() {
    [ -f /etc/steamos-release ] || grep -qi "steamdeck" /sys/devices/virtual/dmi/id/product_name 2>/dev/null
}

# Function to check if a package is installed on Arch
is_installed() {
    pacman -Qi "$1" &>/dev/null
}

# Check for Steam Deck first
if is_steamdeck; then
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}  Steam Deck Detected!${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo ""
    echo -e "${GREEN}Steam Deck uses a read-only filesystem by design.${NC}"
    echo -e "${GREEN}You have two options for building:${NC}"
    echo ""
    echo -e "${YELLOW}Option 1: Use Distrobox (RECOMMENDED)${NC}"
    echo "  1. Install Distrobox from Discover (KDE app store)"
    echo "  2. Create Arch container:"
    echo "     distrobox create --name dev --image archlinux:latest"
    echo "  3. Enter container:"
    echo "     distrobox enter dev"
    echo "  4. Install dependencies in container:"
    echo "     sudo pacman -S gcc pkgconf base-devel"
    echo "     yay -S sdl3-git"
    echo "  5. Run this build script inside the container"
    echo ""
    echo -e "${YELLOW}Option 2: Temporary disable read-only (NOT RECOMMENDED)${NC}"
    echo "  sudo steamos-readonly disable"
    echo "  sudo pacman-key --init"
    echo "  sudo pacman-key --populate archlinux"
    echo "  yay -S sdl3-git gcc pkgconf"
    echo "  ./build.sh"
    echo "  sudo steamos-readonly enable  # Re-enable when done"
    echo ""
    echo -e "${RED}WARNING: System changes will be lost on SteamOS updates!${NC}"
    echo -e "${RED}Use Distrobox for persistent development environment.${NC}"
    echo ""
    echo -e "${YELLOW}Attempting to build with existing tools...${NC}"
    echo ""
    # Don't try to auto-install on Steam Deck, just check and continue
    
elif is_arch; then
    echo -e "${YELLOW}Detected Arch Linux${NC}"
    
    REQUIRED_PACKAGES=("gcc" "pkgconf" "sdl3")
    MISSING_PACKAGES=()
    
    for pkg in "${REQUIRED_PACKAGES[@]}"; do
        if ! is_installed "$pkg"; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done
    
    # Special check for SDL3 - might be sdl3-git from AUR
    if is_installed "sdl3-git" && ! is_installed "sdl3"; then
        echo -e "${GREEN}Found sdl3-git (AUR version)${NC}"
        # Remove sdl3 from missing packages if sdl3-git is installed
        MISSING_PACKAGES=("${MISSING_PACKAGES[@]/sdl3/}")
    fi
    
    if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
        echo -e "${YELLOW}Missing packages detected: ${MISSING_PACKAGES[*]}${NC}"
        
        # Check if pkg-config can actually find SDL3 (might be installed differently)
        if pkg-config --exists sdl3 2>/dev/null; then
            echo -e "${GREEN}But SDL3 is available via pkg-config, continuing...${NC}"
        else
            # Filter out sdl3 if it's in AUR (user needs to install manually)
            PACMAN_PACKAGES=()
            AUR_PACKAGES=()
            
            for pkg in "${MISSING_PACKAGES[@]}"; do
                if [ "$pkg" = "sdl3" ]; then
                    # Check if it's available in official repos
                    if pacman -Si sdl3 &>/dev/null; then
                        PACMAN_PACKAGES+=("$pkg")
                    else
                        AUR_PACKAGES+=("$pkg")
                    fi
                else
                    PACMAN_PACKAGES+=("$pkg")
                fi
            done
            
            # Try to install packages from official repos
            if [ ${#PACMAN_PACKAGES[@]} -gt 0 ]; then
                echo -e "${YELLOW}Attempting to install: ${PACMAN_PACKAGES[*]}${NC}"
                if ! sudo pacman -S --needed --noconfirm "${PACMAN_PACKAGES[@]}" 2>/dev/null; then
                    echo -e "${RED}Could not auto-install packages. Please install manually:${NC}"
                    echo -e "${YELLOW}  sudo pacman -S ${PACMAN_PACKAGES[*]}${NC}"
                fi
            fi
            
            # Warn about AUR packages (don't exit, let build try anyway)
            if [ ${#AUR_PACKAGES[@]} -gt 0 ]; then
                echo -e "${YELLOW}Note: The following packages may need to be installed from AUR:${NC}"
                echo -e "${YELLOW}  ${AUR_PACKAGES[*]}${NC}"
                echo -e "${YELLOW}Install with: yay -S sdl3-git${NC}"
                echo -e "${YELLOW}Attempting build anyway...${NC}"
                echo ""
            fi
        fi
    else
        echo -e "${GREEN}All required packages are installed${NC}"
    fi
fi

# Create necessary directories
mkdir -p assets/maps assets/tokens saves

# Optional: Embed font in executable (run once if you have font.ttf)
# Uncomment to enable:
# EMBED=""
# if [ -f font.ttf ]; then
#     python3 embed_font.py font.ttf > font_embedded.h
#     echo "Font embedded - compiling with -DEMBED_FONT"
#     EMBED="-DEMBED_FONT"
# fi

# Compile STB libraries only if needed (much faster subsequent builds)
if [ ! -f stb_impl.o ]; then
    echo "Compiling STB libraries..."
    gcc -c -O2 -std=c11 stb_impl.c -o stb_impl.o
fi

# Check if SDL3 is available before compiling
if ! pkg-config --exists sdl3 2>/dev/null; then
    echo -e "${RED}ERROR: SDL3 not found via pkg-config!${NC}"
    echo -e "${YELLOW}Please install SDL3:${NC}"
    echo ""
    echo "  Arch Linux:"
    echo "    yay -S sdl3-git"
    echo "    # or"
    echo "    paru -S sdl3-git"
    echo ""
    echo "  Ubuntu/Debian:"
    echo "    sudo apt install libsdl3-dev"
    echo ""
    echo "  Fedora:"
    echo "    sudo dnf install SDL3-devel"
    echo ""
    exit 1
fi

# Compile main.c and link with STB
echo "Compiling main.c..."
gcc -Wall -Wextra -O2 -std=c11 $EMBED \
    main.c stb_impl.o \
    -o vtt \
    $(pkg-config --cflags --libs sdl3) \
    -lm

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    echo "Distribution package should include:"
    echo "  - vtt (executable)"
    echo "  - font.ttf (optional - will use system fonts if missing)"
    echo "  - assets/ folder (for maps and tokens)"
    echo "  - saves/ folder (for save files)"
    echo ""
    echo -e "${GREEN}Run with: ./vtt${NC}"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
