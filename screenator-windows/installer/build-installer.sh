#!/usr/bin/env bash
# Build the Screenator Windows installer with NSIS.
#
# Windows (MSYS2):  ./build-installer.sh
# Linux / macOS:    ./build-installer.sh
# Extra makensis options can be passed through, for example:
#   ./build-installer.sh /DSETUPNAME=Screenator-custom-setup
set -e
cd "$(dirname "$0")"

if command -v makensis >/dev/null 2>&1; then
  MK=makensis
elif [ -x /mingw64/bin/makensis.exe ]; then
  MK=/mingw64/bin/makensis.exe
else
  echo "makensis was not found. Install NSIS or mingw-w64-x86_64-nsis." >&2
  exit 1
fi

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    exec "$MK" "$@" screenator.nsi
    ;;
  *)
    exec "$MK" -DPOSIX_BUILD "$@" screenator.nsi
    ;;
esac
