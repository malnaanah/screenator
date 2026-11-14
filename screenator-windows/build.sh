#!/usr/bin/env bash
# Build a self-contained Screenator.exe for Windows with MSYS2's mingw-w64
# toolchain.  The executable is statically linked, so the portable package
# consists of Screenator.exe at the root and the Screenator Data folder.
set -e
cd "$(dirname "$0")"
WINDOWS_DIR="$(pwd)"
OUTPUT_DIR="$WINDOWS_DIR/../build-windows"
BUILD_DIR="$OUTPUT_DIR/build"
DIST_DIR="$OUTPUT_DIR/dist"
export PATH=/mingw64/bin:$PATH

PDFIUM="third_party/pdfium"
CFLAGS="-std=c11 -O2 -DUNICODE -D_UNICODE -I$PDFIUM/include \
  -DCAIRO_WIN32_STATIC_BUILD -DGLIB_STATIC_COMPILATION -DGOBJECT_STATIC_COMPILATION \
  $(pkg-config --cflags cairo pangocairo glib-2.0 gobject-2.0)"
SLIBS="$(pkg-config --static --libs cairo pangocairo glib-2.0 gobject-2.0 | sed 's/-pthread//g')"
SYSLIBS="-lcomctl32 -lcomdlg32 -lshell32 -lole32 -lshlwapi -lmsimg32 -lgdi32 -luser32 -lhid"

rm -rf "$BUILD_DIR"
rm -rf "$DIST_DIR/Screenator Data"
mkdir -p "$BUILD_DIR" "$DIST_DIR/Screenator Data" "$OUTPUT_DIR/release"

windres -I. -Iassets screenator.rc -O coff -o "$BUILD_DIR/screenator.res"

gcc $CFLAGS src/*.c "$BUILD_DIR/screenator.res" -o "$BUILD_DIR/Screenator.exe" \
  -mwindows -s -static -static-libgcc -static-libstdc++ \
  -Wl,-Bstatic $SLIBS -lstdc++ -liconv -lwinpthread -Wl,-Bdynamic \
  $SYSLIBS "$PDFIUM/lib/pdfium.dll.lib" -lm

cp "$BUILD_DIR/Screenator.exe" "$DIST_DIR/"
cp "$PDFIUM/bin/pdfium.dll" "$DIST_DIR/"
cp -r assets "$DIST_DIR/Screenator Data/"
cp README.md RELEASE_NOTES.md PORTING_STATUS.md COPYING AUTHORS "$DIST_DIR/Screenator Data/"
cp installer/remove-pdf-association.ps1 "$DIST_DIR/Screenator Data/"
cp "$PDFIUM/LICENSE" "$DIST_DIR/Screenator Data/PDFIUM_LICENSE.txt"

echo "Built $DIST_DIR/Screenator.exe (self-contained)"
ls -l "$DIST_DIR/Screenator.exe"
