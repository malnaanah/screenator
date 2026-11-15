#!/bin/sh
set -eu

STAGE="${SCREENATOR_BUILD_STAGE:-/tmp/opencode/screenator-build-stage}"
DL="$STAGE/dl"
RPM_DL="$STAGE/rpmdl"
TOOLS="$STAGE/appimage-tools"

DEV_PKGS="
  libatk1.0-dev libatk-bridge2.0-dev libatspi2.0-dev
  libayatana-appindicator3-dev libayatana-ido3-dev libayatana-indicator3-dev
  libblkid-dev libcairo2-dev libdatrie-dev libdbus-1-dev libdbusmenu-glib-dev
  libepoxy-dev libffi-dev libfontconfig-dev libfreetype6-dev libfribidi-dev
  libgdk-pixbuf-2.0-dev libglib2.0-dev libgraphite2-dev libgtk-3-dev
  libharfbuzz-dev libicu-dev libmount-dev libpango1.0-dev libpcre2-dev
  libpcre3-dev libpixman-1-dev libpng-dev libselinux1-dev libsepol-dev
  libpoppler-dev libpoppler-glib-dev
  libthai-dev libwayland-dev libx11-xcb-dev libxau-dev libxcb1-dev
  libxcb-render0-dev libxcb-shm0-dev libxcomposite-dev libxcursor-dev
  libxdamage-dev libxdmcp-dev libxfixes-dev libxft-dev libxi-dev
  libxinerama-dev libxkbcommon-dev libxrandr-dev libxrender-dev libxtst-dev
  imagemagick librsvg2-bin uuid-dev zlib1g-dev
"

RPM_PKGS="rpm librpm9 librpmio9 librpmbuild9 librpmsign9 rpm-common rpm2cpio debugedit"

mkdir -p "$DL"
cd "$DL"

for pkg in $DEV_PKGS; do
  set -- "$DL/${pkg}_"*.deb
  if [ -e "$1" ]; then
    echo "Already cached: $pkg"
    continue
  fi

  echo "Downloading: $pkg"
  apt-get download "$pkg"
done

mkdir -p "$RPM_DL"
cd "$RPM_DL"

for pkg in $RPM_PKGS; do
  set -- "$RPM_DL/${pkg}_"*.deb
  if [ -e "$1" ]; then
    echo "Already cached: $pkg"
    continue
  fi

  echo "Downloading: $pkg"
  apt-get download "$pkg"
done

mkdir -p "$TOOLS"

fetch () {
  url=$1
  destination=$2

  if [ -s "$destination" ]; then
    echo "Already cached: $destination"
    return
  fi

  echo "Downloading: $destination"
  curl -L --fail --retry 2 -o "$destination.part" "$url"
  mv "$destination.part" "$destination"
}

fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
  "$TOOLS/linuxdeploy-x86_64.AppImage"
fetch "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
  "$TOOLS/linuxdeploy-plugin-gtk.sh"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
  "$TOOLS/appimagetool-x86_64.AppImage"
fetch "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64" \
  "$TOOLS/runtime-x86_64"

chmod +x "$TOOLS/linuxdeploy-x86_64.AppImage" \
         "$TOOLS/linuxdeploy-plugin-gtk.sh" \
         "$TOOLS/appimagetool-x86_64.AppImage" \
         "$TOOLS/runtime-x86_64"

echo "All Linux build dependencies and packaging tools are cached under: $STAGE"
