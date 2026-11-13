#!/usr/bin/env bash
# Build a self-contained Screenator.exe for Windows with MSYS2's mingw-w64
# toolchain.  The executable is statically linked, so the portable package
# consists of Screenator.exe at the root and the Screenator Data folder.
set -e
cd "$(dirname "$0")"
export PATH=/mingw64/bin:$PATH

PDFIUM="third_party/pdfium"
CFLAGS="-std=c11 -O2 -DUNICODE -D_UNICODE -I$PDFIUM/include \
  -DCAIRO_WIN32_STATIC_BUILD -DGLIB_STATIC_COMPILATION -DGOBJECT_STATIC_COMPILATION \
  $(pkg-config --cflags cairo pangocairo glib-2.0 gobject-2.0)"
SLIBS="$(pkg-config --static --libs cairo pangocairo glib-2.0 gobject-2.0 | sed 's/-pthread//g')"
SYSLIBS="-lcomctl32 -lcomdlg32 -lshell32 -lole32 -lshlwapi -lmsimg32 -lgdi32 -luser32 -lhid"

rm -rf build
rm -rf "dist/Screenator Data"
mkdir -p build "dist/Screenator Data"

windres -I. -Iassets screenator.rc -O coff -o build/screenator.res

gcc $CFLAGS src/*.c build/screenator.res -o build/Screenator.exe \
  -mwindows -s -static -static-libgcc -static-libstdc++ \
  -Wl,-Bstatic $SLIBS -lstdc++ -liconv -lwinpthread -Wl,-Bdynamic \
  $SYSLIBS "$PDFIUM/lib/pdfium.dll.lib" -lm

cp build/Screenator.exe dist/
cp "$PDFIUM/bin/pdfium.dll" dist/
cp -r assets "dist/Screenator Data/"
cp README.md RELEASE_NOTES.md PORTING_STATUS.md COPYING AUTHORS "dist/Screenator Data/"
cp installer/remove-pdf-association.ps1 "dist/Screenator Data/"
cp "$PDFIUM/LICENSE" "dist/Screenator Data/PDFIUM_LICENSE.txt"

echo "Built dist/Screenator.exe (self-contained)"
ls -l dist/Screenator.exe
