# Project guidelines: Screenator

## Purpose

Maintain Screenator, a screen annotation app derived from Gromit-MPX 1.4.2, with
small and reviewable changes.

## Layout

- `screenator/`: source code, data files, documentation and `build.sh`.
- `build/`: built `.deb` and AppImage packages.
- `info.txt`: package description, updated whenever the packages change.
- `patches/`: patch files that are numbered 0001-, 0002-, etc.
- `patch-backup/`: backup of patches

## Rules

- The app name is Screenator. Keep the version in sync between
  `screenator/CMakeLists.txt`, the desktop/AppStream metadata and the About
  dialog.
- Keep the GPLv2 `COPYING` file and the upstream Gromit-MPX/Gromit
  attribution in `AUTHORS`, the README, the man page and the source headers.
- Put build artifacts in `build/`; do not leave compiler or packaging
  directories inside the source tree.
- Do not create hidden folders or hidden files inside the working directory.
- Do not make changes to files or directories outside the working directory,
  and ask for confirmation if you have to.
- Do not install the packages, since sudo permission is needed; the user
  installs the built packages.
- Do not test the actual app; the user tests it manually after installation.
- If any change is made to a patch, make a versioned backup of it in `patch-backup` directory.
