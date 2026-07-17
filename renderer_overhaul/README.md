# Renderer Overhaul

Renderer Overhaul replaces ROTH.C's software-rendered 3D world view with a GPU renderer while
leaving the game's interface, text, cursor, and other 2D drawing in the engine. It is under active
development. The current build proves the rendering and compositing path by filling the world
region with a configurable solid color.

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
- `internal_w`, `internal_h`: offscreen render dimensions. Zero follows the current world
  viewport for that dimension.
- `window_mode`: `default`, `windowed`, or `borderless`. `default` leaves the host's selected
  window mode alone.
- `present_mode`: `fifo`, `immediate`, or `adaptive`. Adaptive mode depends on host support.
- `fps_cap`: maximum host presents per second. Zero is uncapped. Internally this is converted to
  a minimum present interval of `1000000 / fps_cap` microseconds.
- `min_present_interval_us`: a direct interval alternative to `fps_cap`. If both are present,
  the later line wins.
- `fov_deg`, `pitch_mode`, `render_mode`: reserved for later renderer features and currently have
  no visual effect. Accepted pitch modes are `horizon_shift` and `true_rotation`; render modes are
  `authentic` and `enhanced`.

## Requirements

The plugin requires a 32-bit OpenGL 3.3 implementation. Linux systems need the 32-bit Mesa or
graphics-driver OpenGL and EGL libraries installed. Windows uses the standard `opengl32` system
library and requires a driver that exposes OpenGL 3.3 core contexts.

## Current behavior

While the 3D world is replaced, save-slot and inventory preview thumbnails render dark. The
held-item drag outline can also key through to the solid world color. These are temporary visual
limitations; normal gameplay state and interaction-cursor classification continue to run in the
engine.
