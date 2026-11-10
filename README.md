# Borderless 1.0 — a Borderless window mode for NBA 2K14 (PC)

Borderless adds **Borderless** to *Video Settings > Window Mode* (Windowed / Fullscreen / Borderless) and keeps the mode you picked: if the game, a crash or a restart switches the window back, Borderless puts it back. It also fixes the black or stale blocks in windowed mode and makes the game's VSync work windowed. It is a [SLAM](https://github.com/Dokitooo/SLAM) mod.

**Download:** [Borderless_v1.0.zip](https://github.com/Dokitooo/Borderless/releases/download/v1.0/Borderless_v1.0.zip) (latest release) ·
[all releases](https://github.com/Dokitooo/Borderless/releases)

## Requirements

- NBA 2K14 PC
- [SLAM](https://github.com/Dokitooo/SLAM/releases/download/v1.0.0/SLAM_v1.0.0.zip) 1.0.0 or newer

## Install

1. Install SLAM first (see its README).
2. Extract the zip into the game folder (the one with `nba2k14.exe`). It contains only:
   ```
   SLAM\Borderless\Borderless.asi
   SLAM\Borderless\Borderless.ini
   ```
3. Start the game. The mod list at the top left shows `Borderless 1.0`.

Pick the mode in *Video Settings*. It is applied and saved when the game asks "Video settings have been changed. Do you want to apply the new settings?" and you answer Yes.

The log is `SLAM\Borderless\Borderless.log`. To remove the mod, delete `SLAM\Borderless`.

## Settings (`Borderless.ini`)

| Setting | What it does |
| --- | --- |
| `Mode` | The window mode, set from the game's Video Settings (`Windowed`, `Fullscreen`, `Borderless`). |
| `VSyncFix` | `1` = the game's VSync also works windowed / borderless. |
| `KeepFrame` | `1` = no black or stale blocks in the picture windowed / borderless. |

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | The mod (`windowmode.c`) |
| `slam/` | SLAM SDK header and glue (`slam_lite.c` = plain `.asi` build) |
| `build/` | Build script (clang-cl on Linux) |
| `package/` | The README and default `.ini` |

## Build

`build/build.sh` builds `bin/SLAM/Borderless.asi` (SLAM mod) and `bin/plain/Borderless.asi` (plain `.asi`, any ASI loader).
On Linux with clang-cl + lld-link (LLVM 18). `slam/slam_glue.c` needs Windows headers: set `MGW` to the
MinGW-w64 `mingw-w64-headers` folder and `GEN` to a folder with a generated `_mingw.h` (see SLAM's `source/build`).

## Verify your download

Every release lists the SHA-256 of the zip in `SHA256SUMS.txt` (also attached to the release).
Compare it with the hash of the file you downloaded:

```powershell
Get-FileHash .\Borderless_v1.0.zip -Algorithm SHA256
```

```bash
sha256sum -c SHA256SUMS.txt
```

## About antivirus warnings

The ASI file in this mod hooks into `nba2k14.exe` at runtime, which some antivirus heuristics
flag on unknown files. To make the release easy to check:

- The full source is in this repository; the release binary is built from it.
- The binary is not packed, compressed or obfuscated, and the zip has no password.
- SHA-256 checksums are published with each release.

If your antivirus still flags a file, you can submit it as a false positive to the vendor, or
build it yourself from the source.
