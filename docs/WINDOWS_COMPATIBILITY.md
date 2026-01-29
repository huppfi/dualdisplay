# Windows Compatibility Guide

## The Problem

When building with MinGW, the executable can depend on MinGW runtime DLLs:
- `libwinpthread-1.dll`
- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`

These DLLs exist on your development machine but not on end users' PCs, causing errors like:
```
The code execution cannot proceed because libwinpthread-1.dll was not found.
```

## The Solution: Hybrid Approach

We use a hybrid approach for maximum compatibility:

### What Gets Statically Linked:
✅ MinGW C runtime (`libgcc`)  
✅ MinGW C++ runtime (`libstdc++`)  

### What Gets Shipped as DLLs:
📦 `SDL3.dll` - Graphics library (required)  
📦 `libwinpthread-1.dll` - Threading library (~50KB, required by SDL3)  

**Why ship pthread DLL?** SDL3 dynamically depends on pthread, and attempting to statically link it causes conflicts. Shipping the small DLL is more reliable than complex static linking tricks.

## Build Flags Explained

```bash
gcc main.c stb_impl.o -o vtt.exe \
  -lSDL3 -lm \
  -static-libgcc \      # Statically link GCC runtime
  -static-libstdc++     # Statically link C++ runtime
```

We use `-static-libgcc -static-libstdc++` to embed the core MinGW runtimes. The pthread library remains dynamic as it's required by SDL3.

## Testing on Clean Windows

To verify your build works on any Windows PC:

1. **Build the release:**
   ```bash
   build.bat
   ```

2. **Test in a clean environment:**
   - Copy `vtt.exe` and `SDL3.dll` to a fresh folder
   - Run on a Windows PC without MinGW/MSYS2 installed
   - Should work without errors

3. **Check dependencies (optional):**
   ```bash
   # In MSYS2:
   ldd vtt.exe
   
   # Should show:
   # - SDL3.dll (dynamic - OK, we ship this)
   # - Windows system DLLs only (KERNEL32.dll, etc.)
   # - NO libwinpthread or libgcc
   ```

## File Size Impact

Static linking increases exe size:
- **Dynamic linking:** ~200 KB
- **Static linking:** ~400 KB

This is acceptable for better compatibility!

## What Users Need

To run the VTT, users only need:
1. ✅ Windows 10/11 (or Windows 7+ with updates)
2. ✅ The release zip contents:
   - `vtt.exe`
   - `SDL3.dll`
   - `libwinpthread-1.dll` (~50KB)
   - `font.ttf` (optional)
   - `assets/` folders

**No MinGW, no MSYS2, no Visual C++ redistributables required!**

All files must be in the same directory for the exe to run.

## Alternative: Fully Static Binary

For a truly single-file executable (no SDL3.dll), you'd need to:
1. Build SDL3 from source with static linking
2. Use `-static` flag (links everything statically)
3. Result: ~5MB exe with no DLL dependencies

**Not recommended** because:
- Much larger file size
- Harder to update SDL3
- License implications (SDL3 is zlib license, allows static linking, but complicates things)

Current approach (static MinGW runtime + dynamic SDL3.dll) is the sweet spot!

## Troubleshooting

### "VCRUNTIME140.dll not found"
You're mixing MinGW and MSVC. Use MinGW only.

### "SDL3.dll not found"
Make sure SDL3.dll is in the same folder as vtt.exe.

### "libwinpthread-1.dll not found"
Make sure libwinpthread-1.dll is in the same folder as vtt.exe. This is included in the release zip.

### How to verify your build

```bash
# In MSYS2 terminal:
ldd vtt.exe

# Should show:
# - SDL3.dll ✅
# - libwinpthread-1.dll ✅
# - Windows system DLLs only (KERNEL32.dll, etc.) ✅
# - NO libgcc_s_seh-1.dll ✅
# - NO libstdc++-6.dll ✅
```

### File sizes
- `vtt.exe`: ~400-500 KB
- `SDL3.dll`: ~1.5 MB
- `libwinpthread-1.dll`: ~50 KB
- **Total**: ~2 MB (very portable!)
