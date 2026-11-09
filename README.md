# Screenator

Screenator is a screen annotation tool for X11, based on
[Gromit-MPX](https://github.com/bk138/gromit-mpx) 1.4.2.

Draw over any running application with a pen, marker, arrows, text, shapes or
eraser, change the line colour, thickness and opacity, choose the text font,
and resize the floating icon. The annotation stays on screen as long as you
need it, and you can keep using your applications while it is visible.

A small draggable on-screen icon (and a matching tray icon) opens the tool and
settings menu.





https://github.com/user-attachments/assets/9b04ff90-14df-458c-ae8f-5d2038690ce3



## Layout

| Path | Contents |
|---|---|
| `screenator-linux/` | Linux source code, data files, documentation and the build script |
| `screenator-linux/src/` | C sources (`screenator.c`, callbacks, config, input) |
| `screenator-linux/data/` | Icons, cursors, default tool configuration, desktop entry, AppStream metadata |
| `screenator-windows/` | Windows source code and build files, prepared separately |
| `info.txt` | Package and feature description |

## Build

Dependencies: GTK+ 3, Ayatana AppIndicator 3, XInput 2, X11, CMake and a C
compiler. On Debian/Ubuntu:

```
sudo apt install build-essential cmake libgtk-3-dev \
    libayatana-appindicator3-dev libxi-dev libx11-dev librsvg2-bin
```

Then:

```
cd screenator-linux
./build.sh
```

The script produces the Linux packages in `../build/`:

- `screenator_8.0_amd64.deb` — install with `sudo apt install ./screenator_8.0_amd64.deb`
- `screenator-8.0-1.x86_64.rpm` — install with `sudo rpm -i ./screenator-8.0-1.x86_64.rpm`
- `Screenator-8.0-x86_64.AppImage` — make executable and run

The Windows application is built separately from `screenator-windows/` with
MSYS2; see `screenator-windows/README.md`.

## Configuration

- `~/.config/Screenator/screenator.ini` — menu settings (colour, width,
  eraser size, opacity, font, shape fill, icon size, last tool, Paper mode
  and shortcuts).
- `~/.config/Screenator/screenator.cfg` — optional per-user tool/button
  mapping; falls back to the bundled (AppImage) or system-wide
  `/etc/screenator/screenator.cfg`.
- `~/.config/Screenator/screenator-annotations.png` — saved Screen-mode
  annotations.
- `~/.config/Screenator/hints.html` — optional user override for the hints.
- `%APPDATA%\Screenator\screenator.ini` — Windows settings.

## License

GNU General Public License version 2 or later. Screenator contains code from
Gromit-MPX 1.4.2 (Copyright 2009–2022 Christian Beier) and Gromit
(Copyright 2000 Simon Budig). See `screenator-linux/COPYING` for the full text.
