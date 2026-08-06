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

- `room_light_reach`: how far a room's own light carries, against the distance the map authored for
  it, from 0.4 through 2. One is the map exactly as written; lower is a darker world, and it
  darkens a dim room long before it touches a lit one. This is the darkness dial.
- `room_light_gain`: a master scale over the light a room gives off, from 0 through 4. One leaves
  it where the map put it.
- `room_light_floor`: the least light any surface receives, from 0 through 0.25. Zero is honest
  darkness.
- `world_light_ceiling`: where the light landing on a surface stops adding up, from 0.4 through 1.
  One cuts it off at the brightest the picture can be drawn, which is what the world image is read
  back into; lower eases it in over the last stretch instead, so two rooms that are both brighter
  than that still read as two rooms rather than as one flat white.
- `room_light_curve`: `engine` reads a map's own brightness value as what the game stores it as —
  the distance that room's light reaches — so a room lights its surfaces until its light runs out.
  `anchored` and `legacy` read it as one flat level for the whole room instead, the first placed
  between the two levels below and the second scaled by the gain above.
- `room_light_dark`, `room_light_lit`: under `anchored` only, what the dimmest rooms of a map give
  off by themselves, from 0 through 1, and what its brightest rooms do, from 0 through 5.
- `room_light_anchor`, `room_light_byte_dark`, `room_light_byte_lit`, `room_light_window_min`,
  `room_light_window_max`: under `anchored` only, which of a map's own brightness values count as
  its dark and its lit ends. Read from the map by default; the rest are for a map that reads
  wrongly.
- `room_light_shape`: how steep the falloff is, from 0.25 through 4. One is the map's own.
- `room_light_unshaded_gain`: how much of the outdoor ground and other surfaces a map marks as never
  to be darkened stays fully bright, from 0 through 1. One is exactly as the map asks.
- `ambient_light`, `ambient_color`, `ambient_intensity`: a little light everywhere, its colour as
  six bare hexadecimal digits, and how much of it there is from 0 through 2.
- `player_light`, `player_light_color`, `player_light_intensity`, `player_light_range`: a real
  light where you are standing, in place of the original's pool on the screen. The reach is in
  world units, from 100 through 4000; smaller is a tighter pool.
- `player_light_flicker`, `player_light_flicker_amount`, `player_light_flicker_speed`: let that
  light breathe the way a flame does. It is held well clear of going out, so it never gutters.
- `sprite_light_gain`: how bright people, creatures and things you can pick up are, from 0 through
  4. Raise it if a dark room makes them hard to find.

Torches, candles and lit sigils give light of their own under `modern`. Which things those are is
the map's own answer rather than a list: the game already marks the objects that are never to be
darkened, and those are exactly the ones that are giving light. Nothing has to be listed for a map
to work, custom maps included.

**These lights cast no shadows**, so one beside a doorway spills a little onto the far side of it
and one behind a thin partition lights both faces. Three things keep that local: a light in a room
you cannot see is not there at all, a light cannot reach past its own room's floor and ceiling by
more than `point_light_z_slack`, and its pool ends where `point_light_range` says rather than
fading away for ever. A light in a room you CAN see goes on lighting it however far off you walk;
how far away you are only stands it further down the queue for one of the slots.

- `point_light`: whether they light anything at all. `point_light_objects` is where they come from;
  turning it off leaves the family on with nothing seeding it.
- `point_light_max`: how many light the picture at once, from 1 through 16. What is dropped is
  always whatever is contributing least where you are standing, and it fades out over
  `point_light_fade` seconds rather than vanishing.
- `point_light_intensity`, `point_light_range`: how bright one is, from 0 through 4, and where its
  pool ends in world units, from 100 through 2000. A fixed torch reads tighter than the light you
  carry.
- `point_light_color_source`, `point_light_color`: `sprite` reads the colour off the thing itself,
  so fire throws fire-coloured light and a cold sigil throws cold light; `fixed` uses the one
  colour beside it, as six bare hexadecimal digits.
- `point_light_glow_scale`: how much of the map's own word about its lantern these take, from 0
  through 2, exactly as the light you carry takes it.
- `point_light_z_slack`: the vertical containment above, from 0 through 512 world units.
- `point_light_hops`: how many doorways from the room you are in a light may still be lighting it,
  from 0 through 3. A light normally goes on lighting its own room for as long as that room is on
  screen whatever this says; it is the limit that comes into force only when the renderer cannot
  tell which rooms are being drawn.

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

It also has one section of a different kind. `[lights]` says what particular THINGS give off, keyed
by the graphic the map draws them with, where a single setting for the whole map is not enough — a
brazier brighter than a candle, or a glowing decal that is plainly not a light at all. It follows
the same three-section chain, and `[profile.NAME.lights]` and `[map.NAME.lights]` are its profile
and per-map forms:

```
[lights]
; graphic = strength, reach, colour     a dash keeps whatever was set before it
0x0412 = 1.6, 520, ffb46e               ; a wall torch
0x0455 = 0.6, 260, -                    ; a table candle, its colour left alone
0x0301 = off                            ; not a light, whatever the map marks it

[map.STUDY1.lights]
0x0412 = 2.0, -, -                      ; this map's torches read brighter
```

Each field is composed on its own, so a map may raise a strength without restating the colour the
global section gave it. Colours here are six bare hexadecimal digits like every other colour in
this file.

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
lands on a whole shade the two are the same picture exactly. Nothing else about the original picture
changes: the light you carry keeps its own shape, its own flicker and its own place. Smooth is the
original with the bands taken out of it, and with nothing else taken out of it.

**Modern** lights the world instead of shading it. Each room glows with the brightness it was built
with, so a room the map calls bright is bright and one it calls dim is dim; a fill and the light
you carry are added to that. Light switches, weapon flashes and everything else the map does to a
room's brightness still work, and read as light rather than as a jump from one shade to the next.

Modern shading brings its own settings with it, all of them dimmed until it is chosen:

- **How far a room's light carries** is the dial that decides what the world feels like. The value a
  map stores for each of its rooms is not a brightness: it is a distance, the range that room's own
  light reaches before it gives out. Read that way, a room lights its surfaces until its light runs
  out — so a corridor's far end goes dark while the wall in front of you does not, and a room whose
  lights are on stays bright down its whole length. At one the map is read exactly as written, which
  is the original's own picture; turning it down takes the dim rooms into darkness long before it
  touches the lit ones, which is what a single brightness dial cannot do because it shrinks the
  difference between them along with everything else. **Room brightness** is a master scale over the
  whole of it, and **Never darker than** is the least light any surface receives.
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
