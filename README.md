# ThemeGallery

EmulationStation theme browser and installer for dArkOSen (RK3326 handhelds).
Browse themes with screenshot previews, then install, reinstall or uninstall them from GitHub without leaving the device.

## Features

- Carousel view with full-screen previews, or a compact list view with thumbnails
- Multiple screenshots per theme, browsed left/right
- Favorites and category filters (All / Favorites / Batocera / FCAMOD)
- Download progress with size, speed and time remaining; cancel at any time
- Installs to `/roms2/themes` when available, otherwise `/roms/themes`
- Detects themes already installed in either location
- Set any installed theme as the EmulationStation theme (applied on exit)
- Restarts EmulationStation on exit when themes were added or removed
- Works offline in browse-only mode
- Supports 640x480, 854x480, 1024x768 and 1280x720

## Controls

A and B follow the EmulationStation **Switch A/B buttons** setting (`InvertButtons`).

| Button        | Action                                         |
|---------------|------------------------------------------------|
| L1 / R1       | Jump 10 themes up / down (wraps around, hold to repeat) |
| Up / Down     | Previous / next theme (wraps around)           |
| Left / Right  | Previous / next screenshot (carousel view)     |
| OK            | Install, or Set Theme / Reinstall / Uninstall if installed |
| Back          | Close dialog / menu                            |
| Y             | Add / remove favorite                          |
| Start         | Menu: View Mode, Filter (All / Favorites / Batocera / FCAMOD), Exit |
| Select + X    | Cancel a download in progress                  |
| Select + Start| Exit immediately                               |

Markers on the preview: green check = installed, red heart = favorite, gold star = current EmulationStation theme, white "b" = Batocera theme.

## Adding themes

Themes are listed in `gallery.ini`. No code changes or rebuild needed.

```ini
[Colorful v2]
download = https://github.com/mistertomlinson/es-theme-colorful-v2/archive/refs/heads/main.zip
images = colorful-v2
author = mistertomlinson
category = batocera
size = 29.8M
```

| Key        | Required | Description |
|------------|----------|-------------|
| `[name]`   | yes      | Section name, shown as the theme title |
| `download` | yes      | GitHub archive zip URL (branch or tag) |
| `images`   | no       | Subfolder of `Images/` holding the screenshots |
| `author`   | no       | Shown under the title |
| `category` | no       | `batocera` or `fcamod` (default). Batocera themes get a "b" badge. Unknown values are treated as `fcamod` |
| `size`     | no       | Download size for progress and time remaining. Bytes, or with `K`, `M` or `G` (1024-based), e.g. `29.8M` |

Supported download URLs:

- `https://github.com/<owner>/<repo>/archive/refs/heads/<branch>.zip` installs as `<repo>`
- `https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip` installs as `<repo>-<tag>` (a leading `v` before a digit is dropped, matching GitHub)

Entries are skipped (and logged) if the URL is not one of the above, the name is a duplicate, or two entries would install to the same folder.

Lines starting with `;` or `#` are comments.

### Screenshots

- Put them in `Images/<images>/`
- Name them by number: `1.png`, `2.jpg`, `3.jpeg` and so on. They are shown in numeric order.
- Image 1 is the list-view thumbnail and the first preview
- Other names are ignored (and logged)
- Any aspect ratio works; each image keeps its own

## Install locations

- **Install target:** `/roms2/themes` if it exists and passes a write test, otherwise `/roms/themes` (created if missing)
- **Installed detection:** both the install target and `/roms/themes` are scanned
- **Reinstall:** replaces the theme in place, in the directory that already holds it (the newest copy if there are two)
- **Uninstall:** removes the copy; if the theme exists in both directories, the oldest copy is removed
- The theme currently set in EmulationStation (`ThemeSet`) cannot be uninstalled

Installs are safe to interrupt: the theme is downloaded and extracted into a temporary folder, then swapped into place. The existing copy is only replaced once the new one is complete. Leftover temporary folders (`.tg_tmp_*`, `.tg_old_*`) are cleaned up on the next launch.

## Files on the device

```
/opt/system/Tools/ThemeGallery.sh          launcher (shown in ES Tools)
/opt/system/Tools/ThemeGallery/
    ThemeGallery                           binary
    gallery.ini                            theme list
    themegallery.cfg                       saved view mode and favorites
    ThemeGallery.log                       log of the last run
    Images/<theme>/1.png ...               screenshots
    fonts/                                 Exo 2
    lib/                                   bundled libraries
```

Settings read from EmulationStation:

- `/home/ark/.emulationstation/es_settings.cfg`: `InvertButtons`, `ThemeSet` (Set Theme writes `ThemeSet`)
- `/etc/emulationstation/es_input.cfg`: controller mapping

## Requirements

- dArkOSen (Debian Trixie, aarch64) with SDL2 (KMSDRM, GLES)
- `unzip`
- CA certificates at `/etc/ssl/certs/ca-certificates.crt` for HTTPS downloads
- An internet connection to download (browsing works offline)

## Building

Builds run on GitHub Actions (`.github/workflows/build.yml`) on an arm64 runner inside a `debian:trixie` container, so the binary matches the device's glibc.

- SDL2_image and SDL2_ttf are built from source (PNG, JPG and SVG only)
- Libraries the binary needs are bundled in `lib/`, except those the device provides (SDL2, glibc, GPU/display)
- The artifact is `ThemeGallery-aarch64.tar.gz`, containing a `Tools/` folder

### Install the build

Extract the tarball into `/opt/system/` (the tar keeps executable permissions):

```bash
tar -xzf ThemeGallery-aarch64.tar.gz -C /opt/system/
```

Remove the old `lib/` first when upgrading, so stale libraries don't remain:

```bash
rm -rf /opt/system/Tools/ThemeGallery/lib
```

## Exit codes

| Code | Meaning |
|------|---------|
| 0    | Normal exit |
| 1    | Startup failure (see the log) |
| 3    | Themes changed; the launcher restarts EmulationStation |

## License

- ThemeGallery: see `LICENSE`
- Exo 2 font: SIL Open Font License 1.1 (`fonts/OFL.txt`)