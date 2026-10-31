#!/bin/sh
#
# build.sh - Build Screenator and package it as a .deb and an AppImage.
#
# The development headers are staged under
# /tmp/opencode/screenator-build-stage, so neither root nor installed -dev
# packages are needed.  If the stage is missing, the packages are downloaded
# with "apt-get download" (no root) and extracted there.
#
# Usage: ./build.sh
#
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT"
BUILD="$ROOT/../build"
DEB="$BUILD/screenator_3.2_amd64.deb"
APPIMAGE="$BUILD/Screenator-3.2-x86_64.AppImage"

STAGE="${SCREENATOR_BUILD_STAGE:-/tmp/opencode/screenator-build-stage}"
DL="$STAGE/dl"
CBUILD="$STAGE/screenator-cbuild"
PKROOT="$STAGE/screenator-pkgroot"
LIBDIR="$STAGE/lib/x86_64-linux-gnu"

DEV_PKGS="
  libatk1.0-dev libatk-bridge2.0-dev libatspi2.0-dev
  libayatana-appindicator3-dev libayatana-ido3-dev libayatana-indicator3-dev
  libblkid-dev libcairo2-dev libdatrie-dev libdbus-1-dev libdbusmenu-glib-dev
  libepoxy-dev libffi-dev libfontconfig-dev libfreetype6-dev libfribidi-dev
  libgdk-pixbuf-2.0-dev libglib2.0-dev libgraphite2-dev libgtk-3-dev
  libharfbuzz-dev libicu-dev libmount-dev libpango1.0-dev libpcre2-dev
  libpcre3-dev libpixman-1-dev libpng-dev libselinux1-dev libsepol-dev
  libthai-dev   libwayland-dev libx11-xcb-dev libxau-dev libxcb1-dev
  libxcb-render0-dev libxcb-shm0-dev libxcomposite-dev libxcursor-dev
  libxdamage-dev libxdmcp-dev libxfixes-dev libxft-dev libxi-dev
  libxinerama-dev libxkbcommon-dev libxrandr-dev libxrender-dev libxtst-dev
  imagemagick librsvg2-bin uuid-dev zlib1g-dev
"

mkdir -p "$BUILD"

# ---------------------------------------------------------------------------
# 1. Prepare the build environment
# ---------------------------------------------------------------------------
if [ ! -d "$STAGE/include/gtk-3.0" ]; then
  echo "==> Downloading development packages (no root required) ..."
  mkdir -p "$DL"
  for pkg in $DEV_PKGS; do
    if ! ls "$DL"/${pkg}_*.deb >/dev/null 2>&1; then
      ( cd "$DL" && apt-get download "$pkg" >/dev/null 2>&1 \
        || echo "WARNING: could not download $pkg" )
    fi
  done

  echo "==> Staging headers, libraries and pkg-config files ..."
  rm -rf "$STAGE/include" "$STAGE/lib" "$STAGE/pkgconfig"
  mkdir -p "$STAGE/include" "$LIBDIR" "$STAGE/pkgconfig"
  for deb in "$DL"/*.deb; do
    tmp="$(mktemp -d)"
    dpkg-deb -x "$deb" "$tmp" >/dev/null 2>&1
    cp -ra "$tmp"/usr/include/. "$STAGE/include/" 2>/dev/null || true
    cp -ra "$tmp"/usr/lib/x86_64-linux-gnu/. "$LIBDIR/" 2>/dev/null || true
    cp -ra "$tmp"/usr/share/pkgconfig/. "$STAGE/pkgconfig/" 2>/dev/null || true
    find "$tmp"/usr/lib -path '*pkgconfig*' -name '*.pc' \
      -exec cp -a {} "$STAGE/pkgconfig/" \; 2>/dev/null || true
    rm -rf "$tmp"
  done

  for pc in "$STAGE"/pkgconfig/*.pc; do
    sed -i 's#^includedir=.*#includedir='"$STAGE"'/include#' "$pc" 2>/dev/null || true
  done

  for lib in atk-1.0 ayatana-appindicator3 ayatana-ido3-0.4 ayatana-indicator3 \
             cairo cairo-gobject dbusmenu-glib gdk-3 gdk_pixbuf-2.0 gio-2.0 \
             glib-2.0 gobject-2.0 gtk-3 harfbuzz pango-1.0 pangocairo-1.0 \
             X11 Xi; do
    target="$(ls -1 /usr/lib/x86_64-linux-gnu/lib${lib}.so.* 2>/dev/null \
              | sort -V | tail -1)"
    if [ -n "$target" ] && [ ! -e "$LIBDIR/lib${lib}.so" ]; then
      ln -sf "$target" "$LIBDIR/lib${lib}.so"
    fi
  done
fi

# ---------------------------------------------------------------------------
# 2. Configure and compile
# ---------------------------------------------------------------------------
echo "==> Configuring and compiling with CMake ..."
rm -rf "$CBUILD"
mkdir -p "$CBUILD"
export PKG_CONFIG_PATH="$STAGE/pkgconfig"
export C_INCLUDE_PATH="$STAGE/include:$STAGE/include/glib-2.0:$LIBDIR/glib-2.0/include:$LIBDIR/dbus-1.0/include"
cmake -S "$SRC" -B "$CBUILD" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_C_FLAGS="-I$STAGE/include -I$STAGE/include/glib-2.0 \
                       -I$LIBDIR/glib-2.0/include \
                       -I$LIBDIR/dbus-1.0/include \
                       -L$LIBDIR" >/dev/null
make -C "$CBUILD" -j"$(nproc)"

# ---------------------------------------------------------------------------
# 3. Build the .deb
# ---------------------------------------------------------------------------
echo "==> Packaging .deb (no root required) ..."
rm -rf "$PKROOT"
mkdir -p "$PKROOT"/DEBIAN "$PKROOT"/usr/bin \
         "$PKROOT"/usr/share/applications \
         "$PKROOT"/usr/share/pixmaps \
         "$PKROOT"/usr/share/icons/hicolor/scalable/apps \
         "$PKROOT"/usr/share/icons/hicolor/256x256/apps \
         "$PKROOT"/usr/share/man/man1 \
         "$PKROOT"/usr/share/metainfo \
         "$PKROOT"/usr/share/screenator \
         "$PKROOT"/etc/screenator \
         "$PKROOT"/usr/share/doc/screenator

install -m 0755 "$CBUILD/screenator" "$PKROOT"/usr/bin/screenator
install -m 0644 "$SRC"/data/screenator.desktop \
   "$PKROOT"/usr/share/applications/
install -m 0644 "$SRC"/data/screenator.png \
   "$PKROOT"/usr/share/pixmaps/screenator.png
install -m 0644 "$SRC"/data/screenator.png \
   "$PKROOT"/usr/share/icons/hicolor/256x256/apps/screenator.png
install -m 0644 "$SRC"/data/screenator.svg \
   "$PKROOT"/usr/share/icons/hicolor/scalable/apps/screenator.svg
install -m 0644 "$SRC"/data/screenator-active.svg \
   "$PKROOT"/usr/share/icons/hicolor/scalable/apps/screenator-active.svg
for icon in pen arrow text free-line spline polygon triangle bucket math bars \
            ellipse rounded-rectangle marker eraser redo undo trash \
            reset-settings hide font-settings shapes paper; do
  install -m 0644 "$SRC"/data/$icon.png "$PKROOT"/usr/share/screenator/$icon.png
done
gzip -9 -c "$SRC"/screenator.1 > "$PKROOT"/usr/share/man/man1/screenator.1.gz
install -m 0644 "$SRC"/data/screenator.appdata.xml \
   "$PKROOT"/usr/share/metainfo/
install -m 0644 "$SRC"/data/screenator.cfg "$PKROOT"/etc/screenator/screenator.cfg
install -m 0644 "$SRC"/README.md "$SRC"/AUTHORS "$SRC"/COPYING \
   "$PKROOT"/usr/share/doc/screenator/

cat > "$PKROOT"/DEBIAN/control <<'EOF'
Package: screenator
Version: 3.2
Section: x11
Priority: optional
Architecture: amd64
Maintainer: Mahmoud Alnaanah <malnaanah@gmail.com>
Depends: libayatana-appindicator3-1 (>= 0.2.92), libc6 (>= 2.34), libcairo2 (>= 1.10.0), libgdk-pixbuf-2.0-0 (>= 2.22.0), libglib2.0-0 (>= 2.39.4), libgtk-3-0 (>= 3.21.4), libx11-6, libxi6 (>= 2:1.2.99.4)
Installed-Size: 320
Description: vibe-coded screen annotation tool based on Gromit-MPX
 Screenator is an on-screen annotation tool for X11.  Draw over any
 running application with a pen, marker, arrow, text, shapes or eraser,
 change the line colour, thickness, eraser size and opacity, and keep
 the annotations visible for as long as you need them.
 .
 A small draggable on-screen icon opens a menu with the drawing tools
 and settings, and the same menu is available from the system tray.
 .
 Screenator is based on Gromit-MPX 1.4.2 and is developed by Mahmoud
 Alnaanah <malnaanah@gmail.com>.
EOF

# Refresh the desktop and icon caches after (un)install so the menu entry
# appears without waiting for the desktop environment to notice the new
# files on its own.
for script in postinst postrm; do
  cat > "$PKROOT"/DEBIAN/$script <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
exit 0
EOF
  chmod 0755 "$PKROOT"/DEBIAN/$script
done

dpkg-deb --root-owner-group --build "$PKROOT" "$DEB" >/dev/null
echo "==> Built: $DEB"

# ---------------------------------------------------------------------------
# 4. Build the AppImage
# ---------------------------------------------------------------------------
echo "==> Preparing AppImage tooling ..."
TOOLS="/tmp/opencode/screenator-appimage-tools"
APPROOT="/tmp/opencode/screenator-appimage"
APPDIR="$APPROOT/Screenator.AppDir"
mkdir -p "$TOOLS"

fetch () {
  # fetch <url> <destination>
  if [ ! -s "$2" ]; then
    curl -L --fail --retry 2 -o "$2.part" "$1" && mv "$2.part" "$2"
  fi
}

fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
      "$TOOLS/linuxdeploy-x86_64.AppImage"
fetch "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
      "$TOOLS/linuxdeploy-plugin-gtk.sh"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
      "$TOOLS/appimagetool-x86_64.AppImage"
chmod +x "$TOOLS"/linuxdeploy-x86_64.AppImage \
         "$TOOLS"/linuxdeploy-plugin-gtk.sh \
         "$TOOLS"/appimagetool-x86_64.AppImage

rm -rf "$APPDIR"
mkdir -p "$APPDIR"/usr/bin \
         "$APPDIR"/usr/share/applications \
         "$APPDIR"/usr/share/icons/hicolor/scalable/apps \
         "$APPDIR"/usr/share/icons/hicolor/256x256/apps \
         "$APPDIR"/usr/share/metainfo \
         "$APPDIR"/usr/share/pixmaps \
         "$APPDIR"/usr/share/screenator

install -m 0755 "$CBUILD/screenator" "$APPDIR"/usr/bin/screenator
install -m 0644 "$SRC"/data/screenator.desktop \
   "$APPDIR"/usr/share/applications/screenator.desktop
install -m 0644 "$SRC"/data/screenator.svg \
   "$APPDIR"/usr/share/icons/hicolor/scalable/apps/screenator.svg
install -m 0644 "$SRC"/data/screenator-active.svg \
   "$APPDIR"/usr/share/icons/hicolor/scalable/apps/screenator-active.svg
install -m 0644 "$SRC"/data/screenator.png \
   "$APPDIR"/usr/share/icons/hicolor/256x256/apps/screenator.png
install -m 0644 "$SRC"/data/screenator.png \
   "$APPDIR"/usr/share/pixmaps/screenator.png
install -m 0644 "$SRC"/data/screenator.appdata.xml \
   "$APPDIR"/usr/share/metainfo/screenator.appdata.xml
install -m 0644 "$SRC"/data/screenator.cfg \
   "$APPDIR"/usr/share/screenator/screenator.cfg
for icon in pen arrow text free-line spline polygon triangle bucket math bars \
            ellipse rounded-rectangle marker eraser redo undo trash \
            reset-settings hide font-settings shapes paper; do
  install -m 0644 "$SRC"/data/$icon.png "$APPDIR"/usr/share/screenator/$icon.png
done

export ARCH=x86_64
export PATH="$TOOLS:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_OUTPUT_VERSION=3.2

# Let the GTK plugin bundle GTK, gdk-pixbuf and the theme engine when the
# helper tools it needs are available.  Otherwise fall back to the host GTK.
if command -v gtk-update-icon-cache >/dev/null 2>&1 \
   && command -v glib-compile-schemas >/dev/null 2>&1; then
  echo "==> Running linuxdeploy (with GTK plugin) ..."
  if ! "$TOOLS/linuxdeploy-x86_64.AppImage" \
        --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/screenator" \
        --desktop-file "$APPDIR/usr/share/applications/screenator.desktop" \
        --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/screenator.png" \
        --plugin gtk; then
    echo "WARNING: linuxdeploy GTK deployment failed; building an AppImage"
    echo "         that uses the GTK libraries of the host system."
  fi
else
  echo "==> GTK helper tools missing, using the host GTK libraries."
fi

# linuxdeploy writes its own AppRun, which does not point the app at the
# bundled configuration and icons.  Replace it afterwards; the GTK hook it
# installs is still sourced here, so the bundled libraries and theme data are
# used.  A plain sh would fail on the bash hook.
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export SCREENATOR_DATADIR="$HERE/usr/share"
export SCREENATOR_CFG="$HERE/usr/share/screenator/screenator.cfg"
for hook in "$HERE"/apprun-hooks/*.sh; do
  [ -e "$hook" ] && . "$hook"
done
exec "$HERE/usr/bin/screenator" "$@"
EOF
chmod +x "$APPDIR/AppRun"
[ -e "$APPDIR/screenator.desktop" ] || \
  cp "$APPDIR/usr/share/applications/screenator.desktop" "$APPDIR/screenator.desktop"
[ -e "$APPDIR/screenator.png" ] || \
  cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/screenator.png" "$APPDIR/screenator.png"
[ -e "$APPDIR/.DirIcon" ] || \
  cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/screenator.png" "$APPDIR/.DirIcon"

echo "==> Building the AppImage ..."
rm -f "$APPIMAGE"
# The appdata id intentionally follows the "screenator" desktop/app id, which
# appstreamcli flags as not reverse-DNS, so skip its metadata check.
"$TOOLS/appimagetool-x86_64.AppImage" -n "$APPDIR" "$APPIMAGE"
chmod +x "$APPIMAGE"
echo "==> Built: $APPIMAGE"

echo
echo "==> Result:"
ls -lh "$DEB" "$APPIMAGE"
