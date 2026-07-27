# Renderer Overhaul

Renderer Overhaul replaces ROTH.C's software-rendered 3D world view with a GPU renderer while
leaving the game's interface, text, cursor, and other 2D drawing in the engine. It is under active
development. The current build supports arbitrary world resolution, Hor+ widescreen and ultrawide
projection, authentic palette lighting and translucency, animated and moving map geometry,
entities, 3D objects, mirrors, parallax backdrops, and engine-composited HUD and menus.

## Installation

Copy the `renderer_overhaul` folder into `<game>/mods/`. Keep `plugin.so` for Linux or
`plugin.dll` for Windows in that folder alongside `renderer_overhaul.cfg`.

The moddable ROTH.C executable loads the plugin automatically. Starting with `--no-mods`, or
removing this folder, restores the original renderer exactly.

## Configuration

Edit `renderer_overhaul.cfg` using `key=value` lines. Keys are case-insensitive and `#` starts a
comment. Saving the file while the game is running reloads it automatically. A changed file must
remain stable across two checks before it is applied, so editors that truncate and rewrite files
cannot expose a partial configuration. If any line is invalid or unknown, the whole reload is
rejected and the current settings remain active. The small file is checked once every 31 rendered
world frames, so the watcher has no meaningful steady-state cost. A resolution change can cause a
one-frame hitch while the render targets are resized.

- `enabled`: `1` or `0`. When disabled, every rendering call passes through unchanged.
- `debug`: `1` prints map geometry and texture archive statistics; `0` keeps them quiet.
- `solid_color`: fallback world color as six hexadecimal `RRGGBB` digits.
- `internal_w`, `internal_h`: world render resolution. Zero follows the engine viewport for that
  dimension. Set both to the desired output resolution, such as `2560` and `1440`.
- `window_mode`: `default`, `windowed`, or `borderless`. `default` leaves the host's selected
  window mode alone; use `windowed` to leave borderless fullscreen at runtime.
- `present_mode`: `fifo` enables vertical synchronization, `immediate` disables it, and `adaptive`
  requests adaptive synchronization when the host supports it.
- `fps_cap`: maximum host presents per second. Zero is uncapped. Internally this is converted to
  a minimum present interval of `1000000 / fps_cap` microseconds.
- `min_present_interval_us`: a direct interval alternative to `fps_cap`. If both are present,
  the later line wins.
- `fov_deg`: horizontal FOV at the original 4:3 aspect, from 30 through 170 degrees. Zero preserves
  the original projection. Wider output aspects automatically gain horizontal view area.
- `pitch_mode`: reserved for a later camera mode. Accepted values are `horizon_shift` and
  `true_rotation`; the current build uses the authentic horizon shift.
- `render_mode`: `authentic` or `enhanced`. Enhanced currently uses the authentic renderer.

## Requirements

The plugin requires a 32-bit OpenGL 3.3 implementation. Linux systems need the 32-bit Mesa or
graphics-driver OpenGL and EGL libraries installed. Windows uses the standard `opengl32` system
library and requires a driver that exposes OpenGL 3.3 core contexts.

## Current behavior

The GPU replaces the 3D world only. The engine continues to own gameplay, collision, interaction
classification, text, menus, the HUD, and the cursor. At wide aspects the authentic engine layer
is centered without stretching while the world fills the configured canvas; gameplay picking uses
the matching wide projection, and engine-only menus retain their original coordinate system.

The in-game settings overlay and enhanced rendering mode are still planned. Configuration is
file-based for now.
