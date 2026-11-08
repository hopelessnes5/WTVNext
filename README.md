**English** · [Русский](docs/README.ru.md)

# WTVNext — Windows Texture Viewer Next Gen

Native Windows tools for direct texture work: a viewer with an authentic classic interface
and a shell extension that renders `.dds` previews right in Explorer.

## Features

### WTVNextApp.exe — texture viewer

A full-fledged, modern analogue of Windows Texture Viewer with an authentic classic interface:

- **Engine** — hardware DirectX 11 with native support for BC1–BC7, BC6H (HDR),
  DXT1–DXT5, cubemaps, as well as TGA, PNG, JPG, HDR.
- **Adaptive display** — automatic centering and scaling of textures of any resolution
  (up to 4K/8K) when resizing the window or expanding to full screen.
- **Controls:**
  - zoom to cursor with the mouse wheel, pan with the left mouse button held down;
  - Mip levels — `↑` / `↓` (and `PgUp` / `PgDn`);
  - previous / next file in the folder — `←` / `→` (and `Space` / `Backspace`);
  - channels `1`–`6` (RGBA, RGB, Alpha, Red, Green, Blue), `A` — RGBA toggle,
    `N` — normal-map Z reconstruction;
  - `F` — fit to window, `Home` / `0` — 100 % zoom.

### WTVNext.dll — shell thumbnail provider

64-bit Shell Thumbnail Provider for Windows Explorer:

- `.dds` previews (BC1–BC7, BC6H/HDR) are rendered directly in Explorer;
- **Integration** — registration and removal of the extension is done in one click
  via **Help → Register Shell Extension (Thumbnails)** in the viewer menu, with an automatic
  administrator-rights request and quick restart of `explorer.exe` for immediate application.

## Repository layout

| Path | Contents |
|---|---|
| `WTVNext/` | COM shell-extension DLL (`WTVNext.dll`) — `.dds` thumbnail provider |
| `WTVNextApp/` | Win32/D3D11 viewer (`WTVNextApp.exe`) |
| `external/DirectXTex/` | single vendored copy of DirectXTex + prebuilt `.lib` |
| `docs/` | refactoring plan, Russian README and related documents |

## Build

Requires Visual Studio 2022 (v143 toolset) and the Windows 10/11 SDK.

1. Open `WTVNext.sln`.
2. Build the **Release | x64** configuration (other configurations are not set up yet —
   see stage 1 of the [refactoring plan](docs/deepseek-WTVNext-refactoring-plan.md)).
3. Artifacts: `x64\Release\WTVNext.dll` and `x64\Release\WTVNextApp.exe`.

DirectXTex is linked from the prebuilt `.lib` files in `external/DirectXTex`;
the full sources and project files are there as well and can be rebuilt if needed.

## Registering `.dds` previews

Run `WTVNextApp.exe` → **Help → Register Shell Extension (Thumbnails)**
(asks for administrator rights, calls `regsvr32`). Use the same menu to unregister.

## Status

Repository hygiene (stage 0 of the plan) is done. Next up — stage 1: bringing the
Debug configuration, parallel builds and the Win32 platform back into shape
(see the plan).
