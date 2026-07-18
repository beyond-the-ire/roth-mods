# Renderer Overhaul

Renderer Overhaul replaces ROTH.C's software-rendered 3D world view with a GPU renderer while
leaving the game's interface, text, cursor, and other 2D drawing in the engine. It is under active
development. The current build renders static map walls, floors, ceilings, and stacked slabs with
the map's palette-index textures and colour tables, including the map's parallax backdrop. Doors,
entities, mirrors, and animated texture frames remain on the development roadmap.

## Installation

Copy the `renderer_overhaul` folder into `<game>/mods/`. Keep `plugin.so` for Linux or
`plugin.dll` for Windows in that folder alongside `renderer_overhaul.cfg`.

The moddable ROTH.C executable loads the plugin automatically. Starting with `--no-mods`, or
removing this folder, restores the original renderer exactly.

## Configuration

Edit `renderer_overhaul.cfg` using `key=value` lines. Keys are case-insensitive and `#` starts a
comment.

- `enabled`: `1` or `0`. When disabled, every rendering call passes through unchanged.
- `debug`: `1` prints map geometry and texture archive statistics; `0` keeps them quiet.
- `solid_color`: the world color as six hexadecimal `RRGGBB` digits.
- `internal_w`, `internal_h`: accepted for compatibility; static-world rendering currently follows
  the world viewport size.
- `window_mode`: `default`, `windowed`, or `borderless`. `default` leaves the host's selected
  window mode alone.
- `present_mode`: `fifo`, `immediate`, or `adaptive`. Adaptive mode depends on host support.
- `fps_cap`: maximum host presents per second. Zero is uncapped. Internally this is converted to
  a minimum present interval of `1000000 / fps_cap` microseconds.
- `min_present_interval_us`: a direct interval alternative to `fps_cap`. If both are present,
  the later line wins.
- `fov_deg` and `pitch_mode`: reserved for later camera modes and currently have no visual effect.
  Accepted pitch modes are `horizon_shift` and `true_rotation`.
- `render_mode`: `authentic` or `enhanced`. Enhanced currently uses the authentic static-world
  renderer.

## Requirements

The plugin requires a 32-bit OpenGL 3.3 implementation. Linux systems need the 32-bit Mesa or
graphics-driver OpenGL and EGL libraries installed. Windows uses the standard `opengl32` system
library and requires a driver that exposes OpenGL 3.3 core contexts.

## Current behavior

Sky materials use a uniform palette index instead of parallax. Animated resources use frame zero,
and translucent surfaces use one destination-index copy per sorted batch; overlapping translucent
surfaces may therefore differ until ordered feedback is added. Save-slot and inventory preview
thumbnails render dark. Normal gameplay state and interaction-cursor classification continue to
run in the engine.
