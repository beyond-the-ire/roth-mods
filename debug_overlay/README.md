# Debug Overlay

A small, read-only technical overlay for
**[ROTH.C](https://github.com/beyond-the-ire/ROTH.C)**. It works with the
original software renderer, Renderer Overhaul, and other callback-based mods.

## Layouts

The `PERFORMANCE` layout can display:

- `PRESENT/s`: host present-loop iterations per second;
- `TICK/s`: observed engine timer ticks per second;
- `COMPOSE/s`: engine-frame compose callbacks per second;
- optional `GAME/s`, rolling present intervals, and applied window state.

`PRESENT/s` is deliberately not called display FPS. It measures calls made by
the host presentation loop and does not prove that a desktop compositor or
physical display showed every call.

The `INSPECTOR` layout can display independently enabled groups for:

- the current RAW map name and DAS resource file;
- raw signed player coordinates, the 0-based player-sector index, and its raw
  record offset;
- raw 16-bit yaw plus requested and applied pitch;
- engine video dimensions and logical aspect;
- the interaction cursor class and the engine's verified raw cursor-surface
  record.

`DETAILED` adds the current sector's ID, glow/light value, floor and roof
heights, bottom and top texture FAT indices, and optional mid-platform heights
and textures. When the cursor is over a wall, it also shows the verified
0-based face index, owning sector index, and bottom, middle, and top face
texture FAT indices.
`ALL` combines `PERFORMANCE` and `DETAILED`.

Unavailable values are shown as `N/A`, `NONE`, or explicitly labeled `RAW`.
The mod does not identify objects or faces from guesses.

The cursor's `DAS FAT INDEX` is the resource-entry index selected for the
current material. `TEXEL U/V` are texture-local coordinates at the cursor's
selected point. A face index is reported only when the cursor result is a wall
surface and its record offset validates against the loaded map's face array.

## Controls

- **F11** cycles `PERFORMANCE` → `INSPECTOR` → `DETAILED` → `ALL` → `OFF`.

This default avoids the base game's F1-F10 actions and Item Grabber's F7
overlay. The key is configurable. F12 is not captured.

The complete layouts are designed around the commonly used 640×480 mode. All
drawing remains clipped safely at smaller resolutions, where the bottom of a
large layout may be omitted.

## Install

Extract the folder into the game's `mods/` directory:

```text
<game>/mods/debug_overlay/plugin.so
<game>/mods/debug_overlay/plugin.dll
<game>/mods/debug_overlay/debug_overlay.cfg
```

The engine loads the binary for the current platform.

## Configuration

`debug_overlay.cfg` is read once when the plugin loads. Boolean fields accept
`0`/`1`, `true`/`false`, `yes`/`no`, or `on`/`off`.

`initial_mode` accepts `performance`, `inspector`, `detailed`, `all`, or
`off`. Each information group has its own switch. `update_hz` controls text
and surface regeneration and is clamped to 1-20 Hz. `anchor` accepts
`top_left`, `top_right`, `bottom_left`, or `bottom_right`.

`mode_key` accepts the same common key names as the first-party keybinds mod,
including `f11`, or a raw set-1 scancode such as `0x57`.

Set `enabled = 0` to leave all callbacks inert. Delete the folder to remove
the mod, or launch with `--no-mods` to disable every mod.

## Technical boundaries

Debug Overlay reads bounded globals plus validated, per-tick records from the
engine-published map-geometry region and draws palette-index pixels through the
SDK's compose callback. It registers no function overrides, calls no engine
functions, writes no game memory, and does not own the full-colour present
channel. It has no Renderer Overhaul dependency or private plugin-to-plugin
interface.

Requires the ROTH.C moddable engine with SDK ABI 0.7 or newer.
