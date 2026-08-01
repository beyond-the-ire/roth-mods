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

## Settings

Press `F12` in the game to open the settings screen. It works everywhere the mod is loaded,
including the main menu and with hardware rendering switched off, and it can be driven with the
keyboard or the mouse:

- arrow keys change the highlighted setting, `Tab` moves between the sections, the settings, and
  the buttons, and `Enter` opens a value for typing or presses a button;
- `Esc` closes an editor, and closes the screen when nothing is open. Leaving with changes that
  were never applied asks first;
- clicking works throughout, including the `<` and `>` on each row.

Changes are collected as a draft and nothing takes effect until **Apply**, which writes the
settings file and puts the whole file into force in one step. **Cancel** returns the draft to the
settings in force, **Defaults** loads the built-in settings, **Original** restores the original
look (640x480, original field of view, every enhancement off), and **Reload From File** reads the
file again after it has been edited by hand.

A change to the render size or the window mode is asked about after it is applied: keep it within
fifteen seconds, or the settings it replaced come back on their own. That is what makes a display
setting the screen cannot show safe to try, and it survives the game being closed or lost in that
window: the next start returns to the settings that were in force before the change.

The **System Information** section reports what the renderer and the host are actually doing,
which is the place to look when a setting and the picture disagree. A row whose setting could not
be honoured shows both, as `(in use: ...)`.

The settings file is written whole, through a temporary file and an atomic replace, so a write
that fails leaves the previous file exactly as it was and reports why on the settings screen.

## Configuration file

The settings screen owns `renderer_overhaul.cfg`, but the file is plain text and can be edited
directly; use **Reload From File** afterwards, or restart the game. Lines are `key=value`, keys
are case-insensitive, and `#` starts a comment. A line with an invalid value rejects that reload
and leaves the settings in force alone; a key this build does not recognize costs only its own
line. Applying the settings rewrites the file with its own comments and ordering.

- `enabled`: `1` or `0`. When disabled, every rendering call passes through unchanged.
- `internal_w`, `internal_h`: world render resolution. Zero follows the engine viewport for that
  dimension. Set both to the desired output resolution, such as `2560` and `1440`. The rendered
  view is never narrower than 4:3.
- `window_mode`: `default`, `windowed`, or `borderless`. `default` leaves the host's selected
  window mode alone; use `windowed` to leave borderless fullscreen at runtime.
- `present_mode`: `fifo` enables vertical synchronization, `immediate` disables it, and `adaptive`
  requests adaptive synchronization when the host supports it.
- `fps_cap`: maximum host presents per second. Zero is uncapped. Internally this is a minimum
  present interval of `1000000 / fps_cap` microseconds, and applying the settings writes it back
  as `fps_cap`.
- `fov_deg`: horizontal FOV at the original 4:3 aspect, from 30 through 170 degrees. Zero preserves
  the original projection. Wider output aspects automatically gain horizontal view area.
- `overlay_key`: which key opens the settings screen, by name, such as `f12` or `f9`.
- `shading_mode`: `authentic` or `smooth`. `authentic` is the original picture; `smooth` follows the
  same shading continuously instead of in thirty-two steps.
- `world_brightness`, `world_contrast`: the picture settings, from 0.5 through 1.5 each. One is the
  original picture for both.

A resolution change can cause a one-frame hitch while the render targets are resized.

## Requirements

The plugin requires a 32-bit OpenGL 3.3 implementation. Linux systems need the 32-bit Mesa or
graphics-driver OpenGL and EGL libraries installed. Windows uses the standard `opengl32` system
library and requires a driver that exposes OpenGL 3.3 core contexts.

## Current behavior

The GPU replaces the 3D world only. The engine continues to own gameplay, collision, interaction
classification, text, menus, the HUD, and the cursor. At wide aspects the authentic engine layer
is centered without stretching while the world fills the configured canvas; gameplay picking uses
the matching wide projection, and engine-only menus retain their original coordinate system.

Authentic rendering is what the mod produces with every enhancement off, so there is no separate
rendering mode to select.

## Enhancements

The Enhancements section of the settings screen holds the settings that change how the world looks
rather than how much of it is drawn. Each one is off by its own value, and everything at that value
is the original picture exactly, to the byte.

**Shading** chooses how the world's light and distance are resolved. **Original** is the picture the
game has always drawn: every surface takes one of the thirty-two shades its map was authored with,
and the step from one to the next is what makes a corridor darken in bands. **Smooth** follows the
same authored shades continuously, reading the colour between two of them rather than snapping to
one, so a wall or a floor darkens evenly. It is the same art and the same palette either way —
nothing is tinted, filtered or recoloured — and at any point where the shading lands on a whole
shade the two are the same picture exactly.

**Brightness** and **Contrast** are the picture settings. Brightness scales the world image and
contrast opens or closes it around mid grey, both in full colour rather than through the original
thirty-two-step shading ladder, so a brighter room keeps the colours it was authored in instead of
stepping through the palette. They apply to the 3D world only: the interface, the HUD, the
inventory and the menus keep their own appearance whatever the picture settings say. One is the
original for both, and returning both to one puts the original picture back exactly.

Moving either setting shows its effect straight away, while the settings screen is still open, so
a picture can be judged against the room it is in. **Cancel**, or leaving the screen without
applying, puts the picture back with the draft; **Apply** keeps it.

The picture settings are one choice for the whole game and are not per-map.
