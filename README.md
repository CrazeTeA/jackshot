# jackshot

A minimalist ShareX alternative for Wayland (primary target: MangoWM): screenshot capture, annotations, saving, clipboard, and uploading via link.

## Dependencies
- Qt6 (Widgets), CMake, C++17 compiler
- Runtime: `grim`, `slurp`, `wl-clipboard`, `curl`, `libnotify`

Arch Linux: `sudo pacman -S qt6-base cmake grim slurp wl-clipboard curl libnotify`

## Building
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

## Usage
```bash
jackshot        # region -> editor
jackshot -f     # fullscreen -> editor
jackshot -d     # region -> save and copy immediately
```

Editor hotkeys: **A** arrow, **R** rectangle, **P** pen, **N** step sticker, **T** text, **Ctrl+Z** undo, **Ctrl+S** save, **Ctrl+C** copy, **Ctrl+U** upload, **Esc** close.

Files: `~/Pictures/Screenshots/`.  
Uploads: `JACKSHOT_UPLOAD_URL` (defaults to [https://0x0.st](https://0x0.st); expects a plain text URL response). For Imgur/Dropbox, an API key is required — add your own implementation in `Share.h`.

## MangoWM Integration
In `~/.config/mango/config.conf`, add a keybind for the Print key and a floating window rule for the app_id `jackshot`. Please refer to the [MangoWM documentation](https://mangowm.github.io/docs/) for the exact `bind` and `windowrule` syntax.

## Contributing
1. Fork the repository and create a branch: `feature/...`.
2. Code style: C++17, 4 spaces indent, no raw `new` outside of Qt-parented objects.
3. Verification: build without warnings + manual testing (`-r`, `-f`, `-d`).
4. Open a pull request with a description and a screenshot.

Roadmap ideas: screen recording (`wf-recorder`), blur/pixelate, OCR, Imgur/S3 integration, system tray.
