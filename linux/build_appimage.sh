#!/bin/sh
# Packs the Linux build into an AppImage (one file to run; Steam Deck: add it to Steam as a non-Steam game):
#   linux/build_appimage.sh <built AladdinCollection> [version]
# Run from the repository's root. Bundles SDL2, FreeType and their libraries (linuxdeploy; the graphics drivers and
# glibc stay the system's), the menus' font (DejaVu Sans from the build machine) with its licence, the readme and the
# licence. Output: dist-linux/Aladdin-Collection-v<version>-x86_64.AppImage (the version: kAppVersion in src/app/app.h).
# Needs linuxdeploy (fetched into .tools/ if missing) and the fonts-dejavu-core package.
set -e
EXE="$1"
VERSION="${2:-$(sed -n 's/.*kAppVersion = "\([^"]*\)".*/\1/p' src/app/app.h)}"
[ -x "$EXE" ] && [ -n "$VERSION" ] || { echo "usage: $0 <built AladdinCollection> [version]   (from the repository root)"; exit 1; }
ROOT=$(pwd)
APPDIR="$ROOT/AppDir"
rm -rf "$APPDIR" dist-linux
mkdir -p "$APPDIR/usr/bin/fonts" dist-linux .tools

cp "$EXE" "$APPDIR/usr/bin/AladdinCollection"
for f in DejaVuSans.ttf DejaVuSans-Bold.ttf; do cp "/usr/share/fonts/truetype/dejavu/$f" "$APPDIR/usr/bin/fonts/"; done
cp /usr/share/doc/fonts-dejavu-core/copyright "$APPDIR/usr/bin/fonts/DejaVu-LICENSE.txt" 2>/dev/null || true
cp readme.txt "$APPDIR/usr/bin/readme.txt"
cp LICENSE "$APPDIR/usr/bin/LICENSE.txt"
cp res/app_icon_preview.png .tools/aladdin.png   # (the Windows icon's 256 x 256 picture)

if [ ! -x .tools/linuxdeploy ]; then
    curl -fsSL -o .tools/linuxdeploy \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x .tools/linuxdeploy
fi
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
OUTPUT="Aladdin-Collection-v$VERSION-x86_64.AppImage" \
    .tools/linuxdeploy --appdir "$APPDIR" -e "$APPDIR/usr/bin/AladdinCollection" \
    -d linux/aladdin.desktop -i .tools/aladdin.png --output appimage
mv "Aladdin-Collection-v$VERSION-x86_64.AppImage" dist-linux/
ls -l dist-linux
