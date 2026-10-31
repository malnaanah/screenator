# Screenator

Screenator is a screen annotation tool for X11, based on
[Gromit-MPX](https://github.com/bk138/gromit-mpx) 1.4.2.

Draw over any running application with a pen, marker, arrows, text, shapes or
eraser, change the line colour, thickness and opacity, choose the text font,
and resize the floating icon. The annotation stays on screen as long as you
need it, and you can keep using your applications while it is visible.

A small draggable on-screen icon (and a matching tray icon) opens the tool and
settings menu.

https://github.com/user-attachments/assets/322afbb5-fffa-472c-b192-504a8db3a756

## Layout

| Path | Contents |
|---|---|
| `screenator/` | Source code, data files, documentation and the build script |
| `screenator/src/` | C sources (`screenator.c`, callbacks, config, input) |
| `screenator/data/` | Icons, cursors, default tool configuration, desktop entry, AppStream metadata |
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
cd screenator
./build.sh
```

The script produces both packages in `../build/`:

- `screenator_3.1_amd64.deb` — install with `sudo apt install ./screenator_3.1_amd64.deb`
- `Screenator-3.1-x86_64.AppImage` — make executable and run

## Configuration

- `~/.config/screenator.ini` — menu settings (colour, width, eraser size,
  opacity, font, shape fill, icon size, last tool).
- `~/.config/screenator.cfg` — optional per-user tool/button mapping; falls
  back to the bundled (AppImage) or system-wide `/etc/screenator/screenator.cfg`.

## License

GNU General Public License version 2 or later. Screenator contains code from
Gromit-MPX 1.4.2 (Copyright 2009–2022 Christian Beier) and Gromit
(Copyright 2000 Simon Budig). See `screenator/COPYING` for the full text.
