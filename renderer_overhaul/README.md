# Renderer Overhaul

Renderer Overhaul replaces ROTH.C's software-rendered 3D world view with a GPU renderer while
leaving the game's interface, text, cursor, and other 2D drawing in the engine. It is under active
development. The current build supports arbitrary world resolution, Hor+ widescreen and ultrawide
projection, authentic palette lighting and translucency, smooth and modern shading with a
configurable light rig, animated and moving map geometry, entities, 3D objects, mirrors, parallax
backdrops, and engine-composited HUD and menus.

## Installation

Copy the `renderer_overhaul` folder into `<game>/mods/`. Keep `plugin.so` for Linux or
`plugin.dll` for Windows in that folder alongside `renderer_overhaul.cfg` and, if you want to light
individual maps differently, `renderer_look.cfg`.

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
- clicking works throughout, including the `<` and `>` on each row;
- a colour has no `<` and `>` of its own: open it with `Enter` or a click and type six hexadecimal
  digits, red then green then blue, such as `806040`.

A setting that needs another one before it can do anything is shown dimmed until that one is set.
The light rig stays dimmed until the shading is **Modern**, which is the only shading that reads
it.

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
be honoured shows both, as `(in use: ...)`, and a row the map being played answers for itself shows
what the map says, as `(this map: ...)`.

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
- `shading_mode`: `authentic`, `smooth`, or `modern`. `authentic` is the original picture; `smooth`
  follows the same authored shading continuously instead of in thirty-two steps; `modern` lights
  the world with the rig below instead of shading it by distance.
- `world_brightness`, `world_contrast`: the picture settings, from 0.5 through 1.5 each. One is the
  original picture for both.

The light rig applies to `modern` shading only. Every one of these keys is ignored by the other two
shadings, so setting one while the shading is `authentic` leaves the picture untouched.

- `room_light_gain`: how much light a room gives off from the brightness it was built with, from 0
  through 4. One is the brightness the map asks for.
- `room_light_floor`: the least light any surface receives, from 0 through 0.25. Zero is honest
  darkness.
- `ambient_light`, `ambient_color`, `ambient_intensity`: a little light everywhere, its colour as
  six bare hexadecimal digits, and how much of it there is from 0 through 2.
- `player_light`, `player_light_color`, `player_light_intensity`, `player_light_range`: a real
  light where you are standing, in place of the original's pool on the screen. The reach is in
  world units, from 100 through 4000; smaller is a tighter pool.
- `player_light_flicker`, `player_light_flicker_amount`, `player_light_flicker_speed`: let that
  light breathe the way a flame does. It is held well clear of going out, so it never gutters.
- `sprite_light_gain`: how bright people, creatures and things you can pick up are, from 0 through
  4. Raise it if a dark room makes them hard to find.

A resolution change can cause a one-frame hitch while the render targets are resized.

## Per-map look file

`renderer_look.cfg`, beside the settings file, says how individual places are lit. The settings
file says what you want everywhere; this one says what a particular map wants, and where it gives
an answer that answer is the one in force. It is optional, the game never writes it, and it takes
the same `key=value` lines with `#` starting a comment — so a colour in it is six bare hexadecimal
digits with no hash in front.

It has three kinds of section, and a later one wins over an earlier one: `[look]` for every map,
`[profile.NAME]` for a named set applied wherever a map asks for it, and `[map.NAME]` for one map,
named by its own file without the folder and without the extension. A map picks up a profile with
a `profile = NAME` line and may add keys of its own, which win over the profile's.

Only the settings that describe how a place looks may appear: the shading, the light rig, and the
world gain. A line the file cannot use costs that line and nothing more, and the game reports it
when it reads the file. **Reload From File** on the settings screen reads this file again too.

The file ships with a `day` and a `night` profile and nothing pointed at them, so the pair is there
to read and change without pre-judging any map. A row a map answers for is marked on the settings
screen with what the map says, because moving that setting will not change the picture where you
are standing.

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

**Shading** chooses how the world's light and distance are resolved, and everything else in this
section follows from it.

**Original** is the picture the game has always drawn: every surface takes one of the thirty-two
shades its map was authored with, and the step from one to the next is what makes a corridor darken
in bands.

**Smooth** follows the same authored shades continuously, reading the colour between two of them
rather than snapping to one, so a wall or a floor darkens evenly. It is the same art and the same
palette either way — nothing is tinted, filtered or recoloured — and at any point where the shading
lands on a whole shade the two are the same picture exactly. The pool of light around you stops
jittering, becomes round instead of square, and is one pool rather than two.

**Modern** lights the world instead of shading it. Each room glows with the brightness it was built
with, so a room the map calls bright is bright and one it calls dim is dim; a fill and the light
you carry are added to that; and distance stops darkening anything by itself. Light switches,
weapon flashes and everything else the map does to a room's brightness still work, and read as
light rather than as a jump from one shade to the next.

Modern shading brings its own settings with it, all of them dimmed until it is chosen:

- **Room brightness** and **Never darker than** are the two ends of how dark the world is allowed
  to get: the first scales what rooms give off, the second is the least light any surface receives.
- **Ambient fill**, with its **colour** and **strength**, is a little light everywhere, so what
  nothing else reaches is still faintly visible.
- **Carried light**, with its **colour**, **strength** and **reach**, is a real light where you are
  standing rather than a pool painted on the screen: surfaces facing you catch it, surfaces turned
  away do not, and it is correct in a mirror without being told anything about one. **Flickers**,
  with its **depth** and **speed**, lets it breathe like a flame; it is held well clear of going
  out, so it never gutters or strobes.
- **Item and figure brightness** is people, creatures and things you can pick up, on a dial of
  their own so they never disappear in a dark room.

**Brightness** and **Contrast** are the picture settings. Brightness scales the world image and
contrast opens or closes it around mid grey, both in full colour rather than through the original
thirty-two-step shading ladder, so a brighter room keeps the colours it was authored in instead of
stepping through the palette. They apply to the 3D world only: the interface, the HUD, the
inventory and the menus keep their own appearance whatever the picture settings say. One is the
original for both, and returning both to one puts the original picture back exactly.

Moving any of these settings shows its effect straight away, while the settings screen is still
open, so a picture can be judged against the room it is in — the shading itself included.
**Cancel**, or leaving the screen without applying, puts the picture back with the draft; **Apply**
keeps it. A setting the map being played answers for itself keeps the map's answer while you move
it, and the row says so.

The picture settings are one choice for the whole game and are not per-map; the shading and the
light rig may be set per map through the look file.
