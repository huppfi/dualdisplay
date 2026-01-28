# README Images Directory

This directory contains images and GIFs used in the main README.

## Required Files

See `docs/IMAGES_NEEDED.md` for detailed specifications.

### Images (9 files needed):

1. ✅ `vtt-overview.png` - Overview screenshot
2. ✅ `dual-window.gif` - Dual window demo
3. ✅ `token-management.gif` - Token manipulation
4. ✅ `fog-of-war.gif` - Fog of war painting
5. ✅ `damage-tracking.gif` - Damage system
6. ✅ `condition-wheel.gif` - Condition wheel UI
7. ✅ `squad-drawing.gif` - Squads and drawings
8. ✅ `save-load.png` - Save system screenshot
9. ✅ `grid-calibration.gif` - Grid calibration

## File Naming

**IMPORTANT:** Filenames are case-sensitive and must match exactly:
- Use lowercase
- Use hyphens (not underscores)
- Use `.png` for screenshots
- Use `.gif` for animations

## Creating Images

See `../IMAGES_NEEDED.md` for:
- What to show in each image
- Recommended tools
- Optimization tips
- Size guidelines

## Testing

After adding images, test locally:

```bash
# View README with images
grip README.md  # if you have grip installed

# Or just push to GitHub and check there
git add docs/images/
git commit -m "Add README images"
git push
```

The README will automatically show the images once they're in this directory!
