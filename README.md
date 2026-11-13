# Screenator

Screenator is a screen annotation tool for Linux and Windows, based on
[Gromit-MPX](https://github.com/bk138/gromit-mpx) 1.4.2. Version 8.0 brings
Paper Drawing and PDF support to Linux and Windows, with a shared set of
features and platform-specific implementations.

Draw over applications with the pen, marker, text, eraser and shape tools.
Paper mode provides Plain, Ruled, Graph and PDF documents with multiple pages,
continuous scrolling, page navigation and undo. Save paper documents as PDF
with vector annotations. The configurable tool bar, editable shortcuts and
updated input controls are available on both platforms.

## Documentation

- [Linux guide](screenator-linux/README.md)
- [Windows guide](screenator-windows/README.md)
- [Screenator 8.0 release notes](release-notes-8.0.md)
- [Windows 8.0 release notes](screenator-windows/RELEASE_NOTES.md)

## Build Linux packages

Dependencies: GTK+ 3, Ayatana AppIndicator 3, XInput 2, X11, CMake and a C
compiler. On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake libgtk-3-dev \
    libayatana-appindicator3-dev libxi-dev libx11-dev librsvg2-bin
cd screenator-linux
./build.sh
```

The script creates the DEB, RPM and AppImage packages in `build/` at the
repository root:

- `screenator_8.0_amd64.deb`
- `screenator-8.0-1.x86_64.rpm`
- `Screenator-8.0-x86_64.AppImage`

See [the Windows guide](screenator-windows/README.md) for building the native
Windows application with MSYS2.

## Configuration

- Linux menu settings: `~/.config/Screenator/screenator.ini`
- Linux per-user tool/button mapping: `~/.config/Screenator/screenator.cfg`
- Windows settings: `%APPDATA%\Screenator\screenator.ini`

## License

GNU General Public License version 2 or later. Screenator contains code from
Gromit-MPX 1.4.2 (Copyright 2009–2022 Christian Beier) and Gromit
(Copyright 2000 Simon Budig). See the platform `COPYING` files for the full
text.
