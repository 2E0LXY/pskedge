# assets/

Image assets for the installer and in-app splash screen. Referenced by
CMakeLists.txt (CPack/NSIS settings) and src/main.cpp (QSplashScreen).
All entries here are optional — CMake and main.cpp both check whether the
file exists before using it, so the build and app work fine without any of
these present.

| File | Used for | Required format |
|---|---|---|
| `splash.png` | In-app splash screen shown while the program loads | PNG, any size (scaled to fit) |
| `installer_header.bmp` | NSIS installer header banner (top of each page) | BMP, 150x57 px |
| `installer_welcome.bmp` | NSIS installer welcome/finish page sidebar image | BMP, 164x314 px |
| `pskedge.ico` | Windows executable/installer icon | ICO, multi-resolution (16/32/48/256px) recommended |

NSIS requires the banner/sidebar images in those exact pixel dimensions -
a differently-sized source image needs to be cropped/resized to match, not
just dropped in as-is.
