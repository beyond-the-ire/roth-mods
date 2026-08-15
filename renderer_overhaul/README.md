# Renderer Overhaul

Renderer Overhaul replaces ROTH.C's software-rendered 3D world view with a GPU renderer while
leaving the game's interface, text, cursor, and other 2D drawing in the engine. It is under active
development. The current build supports arbitrary world resolution, Hor+ widescreen and ultrawide
projection, authentic palette lighting and translucency, smooth and modern shading with a
configurable light rig, animated and moving map geometry, entities, 3D objects, mirrors, parallax
backdrops, and engine-composited HUD and menus.

## Installation

Copy the `renderer_overhaul` folder into `<game>/mods/`. Keep in that folder:

- `plugin.so` for Linux, or `plugin.dll` for Windows;
- `renderer_overhaul.cfg`, the settings file;
- `game_configs/`, which holds the descriptions of how individual maps are lit. The base game's
  own, `game_configs/roth.json`, ships in it and is selected out of the box.

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

The **Enhancements** section is long, so its settings are gathered under headings — **Game
config**, **Shading and picture**, **Room light**, **The light you carry**, **Items and figures**,
**Light sources** and **Sun and moon** — and each heading opens and closes. `Enter` or a
click on a heading opens or closes it, and the arrow keys do the same: left folds it away, right
opens it. A heading shows `+` when it is closed and `-` when it is open.

The section opens showing only what is doing something: a heading opens by itself when it holds a
setting standing somewhere other than where it ships, or one the map you are in answers for.
**Shading and picture** is always open, because everything below it is a dial on the shading law
and does nothing at all until that law is chosen. Nothing in force is ever folded out of sight: a
heading carries a `*` when something under it has been changed, and it carries the red mark, and
says the map sets its own, when the map you are in answers for anything under it. Which headings
are open is how you are reading the screen at that moment rather than a setting — it is never
written to the settings file, and the section works out again which ones to open every time the
screen is opened.

Changes are collected as a draft and nothing takes effect until **Apply**, which writes the
settings file and puts the whole file into force in one step. **Cancel** returns the draft to the
settings in force, **Defaults** loads the built-in settings, **Original** restores the original
look (640x480, original field of view, every enhancement off), and **Reload From File** reads the
settings file again after it has been edited by hand — and, with it, lists the game configs folder
again, so a file dropped in there while the game is running turns up without a restart.

A change to the render size or the window mode is asked about after it is applied: keep it within
fifteen seconds, or the settings it replaced come back on their own. That is what makes a display
setting the screen cannot show safe to try, and it survives the game being closed or lost in that
window: the next start returns to the settings that were in force before the change.

The **System Information** section reports what the renderer and the host are actually doing,
which is the place to look when a setting and the picture disagree. A row whose setting could not
be honoured shows both, as `(in use: ...)`, and a row the map being played answers for itself shows
what the map says, as `(this map: ...)`.

Where the map you are in answers for a setting itself, the settings screen says so in red, and red
on that screen means nothing else. The value of every such row is drawn in it; a line under the
section names the map whenever that section holds one of those rows; and moving one of them says,
once, that the map you are in sets it itself and your change takes effect elsewhere. Every setting
still applies everywhere the map does not answer for it, so a change made there is not lost — it is
waiting outside.

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
- `game_config`: which file in `game_configs/` describes this game's maps, by its name without the
  `.json` — `roth` for the one that ships. An empty value, and no line at all, both mean none: every
  map falls back to the settings alone. See *The game configs folder* below.
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
- `room_light_floor`: the least any surface is ever left showing, from 0 through 0.25, as a share
  of the brightest the picture can be drawn — a hundredth is about two levels out of 255, a tenth
  about twenty-five. It lifts what has no light and leaves what has alone, so a dark room stays
  dark and stays readable rather than going to one flat wash. Zero is honest darkness.
- `room_light_shoulder`: how softly a room's light ends, in shade steps, from 0 through 4. A
  room's light has to stop somewhere, and stopping it dead draws a rim around you at the distance
  it stops; this fades the last of it out instead. One step, which is what it ships at, is the
  largest that is provably never reached when the reach above is one — so it costs the map read as
  written nothing and eases exactly what a shortened reach made. Zero is the hard stop.
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
  six bare hexadecimal digits, and how much of it there is from 0 through 2. What it was for is
  what `room_light_floor` does now, and does without lifting the lit end of the picture with it, so
  it is off the settings screen; it stays here for the one thing it can still say that a floor
  cannot, which is what colour a darkness is.
- `player_light`, `player_light_color`, `player_light_intensity`, `player_light_range`: a real
  light where you are standing, in place of the original's pool on the screen. It ships lit,
  because the room response above lets a room's own light run out and a person then needs one of
  their own. The reach is in world units up to 4000; **zero, which is what it ships at, follows
  the reach the map itself authored for its own candlelight** — every map states the shade level
  its candle-lit surfaces are drawn at, and that level is a distance. Smaller is a tighter pool,
  and the settings screen shows what the map is actually giving you.
- `player_light_forward`: how far in front of you that light sits, as a share of how far it
  reaches, from 0 through 0.5. The game's own candlelight is not a point on top of you: it is
  measured on the screen from an origin about the middle of the view, which makes it a cone about
  the direction you are looking, fourteen degrees wide on every map the game ships. Aiming the pool
  forward is that geometry rather than a departure from it — the light carries further down the
  corridor you are facing and less to either side. What moves is where the pool falls off from; the
  light is still held where you are, so the wall two paces in front of you is lit rather than lit
  from behind. Zero — where it ships, since the candle cone itself already aims where you look —
  puts it on top of you.
- `player_light_penumbra`: how soft your candlelight's edge is, from 0 through 1. The game switches
  its cone from light to dark at one pace — a flashlight's rim; this spreads the transition over a
  wider band about the same edge, a lantern's soft falloff. The middle of the cone and the darkness
  well beyond it stay where they are. Zero is the game's own edge, exactly.
- `player_light_flicker`, `player_light_flicker_amount`, `player_light_flicker_speed`: let that
  light move the way the game's own flame does. The game flickers its candlelight by moving the
  pool about where you are looking, not by pulsing its brightness, and that is what these do: the
  depth is how far the pool wanders — full is the game's own throw — and the speed is the pace.
  It never strobes and never gutters out.
- `sprite_light_gain`: how bright people, creatures and things you can pick up are, from 0 through
  4. Raise it if a dark room makes them hard to find.

The seven keys that only `anchored` reads are not offered on the settings screen and are not written
into a new settings file. They are still read, and a file that already carries one keeps it through
every save, so a look tuned on that response goes on working. Under `engine` a map's brightness
values are also no longer counted or scaled when it loads: the value each room carries means the
same thing on every map, so there is nothing to fit.

The light sources a map places — torches, candles, lit sigils, glowing surfaces — give light of
their own under `modern`. Which things those are is
the map's own answer rather than a list: the game already marks the objects that are never to be
darkened, and those are exactly the ones that are giving light. Nothing has to be listed for a map
to work, custom maps included.

**The strongest few of them cast real shadows.** `point_light_shadows` says how many, and the ones
that do are the ones lighting most of what you can see — so shadows arrive from the nearest and
strongest outward as you walk, and a light joins or leaves the set with a rebuild of its own map
rather than a flicker. A light past the count spills a little onto the far side of a doorway and
lights both faces of a thin partition. Three things keep even that local: a light whose pool falls on
nothing you can see is not there at all, a light cannot reach past its own room's floor and ceiling
by more than `point_light_z_slack`, and its pool ends where `point_light_range` says rather than
fading away for ever. What decides whether a light is there is always where its light FALLS and
never whether you can see the lamp itself: step behind a pillar so the flame is hidden and the pool
it casts is still on the wall in front of you. A light lighting something you CAN see goes on
lighting it however far off you walk; how far away you are only stands it further down the queue for
one of the slots.

- `point_light`: whether they light anything at all. `point_light_objects` is where they come from;
  turning it off leaves the family on with nothing seeding it.
- `point_light_max`: how many light sources may be lighting the picture at once, from 1 through
  256, worth-most first, so a hall full of them shows every pool across the room rather than the
  nearest few. It ships at 32. The top of the dial is the renderer's own light table: each light
  is four rows of a uniform block, and 256 of them is exactly the 16 kilobytes every graphics
  driver of this generation guarantees. A light entering the set comes up over `point_light_fade`
  seconds; only once the dial is full does the least contributing one stand down. It also holds
  `point_light_shadows` below, which stops sooner for a reason of its own storage.
- `point_light_shadows`: how many of those cast real shadows, from 0 through 64 — a ceiling of its
  own, the shadow atlas's rather than the light table's, because the six-face maps stand in one
  texture column. Nought is none at
  all; anything from `point_light_max` upward is every light there is, so a big number here means
  "all of them" whatever the row above is set to. The ones that get them are the ones worth most
  where you are standing, and a light holding a shadow keeps it until another clearly outranks it,
  so a shadow does not blink on and off at a boundary. Each shadowed light is six small renders of
  its surroundings — taken again only when something within reach of it moves — plus one lookup per
  lit pixel per frame. Measured over a whole lit map: at 1920x1080 four of them draw about a third
  slower than none and eight about two thirds slower; at 3840x1600 it is nearer two fifths and four
  fifths. A game config may pin one light's shadows on or refuse them whatever this says.
- `point_light_intensity`, `point_light_range`: how bright one is, from 0 through 4, and where its
  pool ends in world units, from 100 through 2000. A light fixed in the world reads tighter than
  the light you carry.
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

## The game configs folder

The settings screen says what you want everywhere. A **game config** says what a particular place
wants, and where it gives an answer, that answer is the one in force there.

Those descriptions live in `game_configs/`, inside the mod folder, one file per game or map pack,
each a `.json` file. The base game's own ships as `game_configs/roth.json`. Which one is in force
is the **Config file in use** row, under the **Game config** heading at the top of the Enhancements
section on the settings screen: it lists every file the folder holds, plus **None**, and choosing
None — or taking the file out of the folder — leaves every map falling back to the settings screen
alone.

A file is shown in that list by its own `for` line rather than by its file name, so
`roth.json` reads as *Realms of the Haunting*. A file with no `for` line is shown by its name. Name
a file with letters, digits, `-`, `_` and `.` only: the name is what the settings file records the
selection as, and one carrying a space or a `#` cannot be written down, so it is left out of the
list and the game says why.

**Editing the chosen file while the game is running is the point of it.** Save it, then press
**Reload game config** — the button directly under that picker on the settings screen.
It reads the chosen file from disk and puts the whole of it back into force where you stand: the
dials it sets come back into force, and the lights it grants or hangs on the map you are in are restaged
without waiting for a map change — a light you added appears, one you removed goes out. If a save
has a mistake in it — a missing brace, a stray comma — nothing is lost: whatever was in force
stays in force, the game says what is wrong with the file once, and the next good save is read
when you press the button again. (For mod development there is also a file watcher that picks up
saves on its own: start the game with `HWR_CFG_WATCH=1`; it is off otherwise.)

**Reload From File** on the settings screen reads the settings again, lists the folder again, and
reads the chosen description again through that same complete road — so a file dropped into that
folder while the game is running turns up in the list without a restart. If the settings file
names a description that is no longer in the folder, nothing else is disturbed: the rest of your
settings load exactly as they were, the list keeps the name with `(missing)` beside it, every map
falls back to the settings alone, and putting the file back puts the selection back.

### What is in one

```json
{
  "format": 1,
  "for": "Realms of the Haunting",
  "notes": "How this game's places are lit.",

  "everywhere": {
    "settings": { "shading_mode": "modern", "room_light_reach": 0.85 },
    "lights": {
      "sprites": {
        "34/0": { "strength": 1.6, "reach": 520, "colour": "#ffb46e" }
      },
      "textures": {
        "100": { "colour": "#7fe0a0", "strength": 1.2, "reach": 640 }
      }
    },
    "environment_lighting": {
      "colour": "#ffede0", "strength": 0.8, "elevation": 45
    }
  },

  "maps": {
    "STUDY1": {
      "notes": "Lamplit and enclosed; its own candles carry it.",
      "settings": { "room_light_reach": 0.72 },
      "environment_lighting": { "direction": 210 },
      "lights": {
        "sprites": { "34/0": { "strength": 2.0 } },
        "objects": [
          { "notes": "the standing candelabra, which the map never lit",
            "index": 70,
            "colour": "#ffb46e", "strength": 1.4, "reach": 420 },
          { "notes": "every arcane ward its author gave one ID",
            "id": 36, "reach": 300 }
        ],
        "faces": [
          { "notes": "the sigil wall in the north room",
            "index": 2430, "colour": "#7fe0a0", "strength": 0.8 }
        ],
        "placed": [
          { "notes": "a lamp over the stairwell the map never had",
            "at": [1284, -2210, 96], "colour": "#ffb46e",
            "strength": 1.2, "reach": 420 }
        ]
      }
    }
  }
}
```

`everywhere` and each entry under `maps` have the **same shape**, and that is the whole of the
composition rule: a map's answer is laid over the game's answer, field by field. A map nobody
listed gets `everywhere` alone, silently, so a map pack is covered whether or not anyone has
described it.

A map is keyed by the name of its **own file**, without the folder and without the extension —
`STUDY1`, `CRYPT1` — matched whatever its case and with or without an extension either side, so
`study1` and `STUDY1.RAW` name one map. Maps that share a texture archive are still different maps
here.

`settings` holds settings-screen keys, spelled exactly as the settings file spells them and read by
exactly the same rules, so every bound and every message comes from the settings screen itself and
the two can never disagree. A key belongs there when it says how a *place* is lit rather than what
a *screen* wants: the shading and the whole light rig may be written in one, and the brightness and
contrast of the picture may not. A row a map answers for is marked in red on the settings screen
with what the map says, because moving that setting will not change the picture where you are
standing.

`environment_lighting` is the third thing a place may say, and it describes the sun or the moon —
the light that comes down out of the sky rather than standing in a room. It is the one part of a
place that is usually aimed per map, and it has [a section of its own](#the-skys-own-light-environment_lighting)
below.

**Every number in every one of these tables is decimal, and every one of them is a number the ROTH
Editor prints for the same thing.** Write what is on your screen.

#### The one-glance rule

> **An object gives light of its own when its `Lighting` field reads 0 in the editor. A grant makes
> anything a light — marked or not — and a thing made one is drawn lit as well.**

That is the whole of it. Select a thing in the editor: if `Lighting` is 0 the game already lights the
room with it and a grant is *tuning*; if it is anything else the game draws it as an ordinary object,
and naming it in a grant is what turns it into a light. A thing made a light that way is also **drawn
undarkened**, exactly as the map's own lights are, so it looks like the thing lighting the room rather
than a dark sprite with a halo round it. Where that is not wanted — a thing that should give light and
still sit in the room's own shading — the entry says `"lit": false` (see the field below).

**The colour is optional wherever there is a picture to read it off.** A thing is drawn with a
picture and a surface is painted with one, so leaving `colour` out takes the colour from that picture:
a torch throws its own flame's colour, a cold ward its own. Only `placed` has no picture of any kind
behind it, and only `placed` therefore requires a colour to be written.

**A thing's light comes from the middle of the thing.** A map hangs an object by its feet or by its
head, and a light at that point would sit below the flame or above it: a torch would wash the floor
and leave the wall beside it dark. So the light is emitted from halfway up the sprite as it is
actually drawn. And a torch, sconce or ward mounted flat on a wall is hung a few units off it, close
enough that its own wall would only ever be grazed by its light and would stay dark while the floor,
the ceiling and the far wall lit up — so a mounted thing's light stands clear of the wall it is
mounted on, and the wall behind it is lit like any other surface. A thing standing out in the room is
lit from where it stands and is not moved at all.

#### `lights` — five tables

`lights` holds five tables. Two are **blankets** over a whole game, three name **particular things in
one map**:

| table | keyed by | reaches |
|---|---|---|
| `sprites` | either number the editor prints for one picture: the **Texture Index / Texture Source** pair, `"34/0"`, or the single picture number, `"4130"` | every object drawn with that sprite |
| `textures` | a texture number, `"100"` | every wall band and every floor and ceiling painted with it |
| `objects` | `"index"` (`Object: N`) or `"id"` (`ID:`) | one object, or one group sharing an ID |
| `faces` | `"index"` (`Face: N`) or `"id"` (`Face ID`) | one face, or one group sharing a Face ID |
| `placed` | three numbers | nothing at all — a light where the map has none |

Each entry says what the thing gives off, and each field stands on its own:

- **`colour`** — `"#rrggbb"`. JSON has no comment character for a hash to collide with, so a colour
  here is written the way a person writes one; the bare six digits are accepted too.
- **`strength`** — how bright, over the settings screen's own value.
- **`reach`** — how far, in world units, over the settings screen's own value.
- **`shadows`** — the last word about this one light, over the `point_light_shadows` count. Say
  nothing and the light is one of the count's own candidates, which is the ordinary case. `true`
  **pins** it: it takes a shadow ahead of the count's picks, wherever it stands in the ranking —
  which is how a creator says *this* lamp is the one that matters in this room. It is not free, it
  spends one of the count's own places. `false` refuses this light a shadow whatever the count says,
  and refuses one a broader entry pinned. What a shadowed light is blocked by: the walls, floors,
  doors and things standing in its reach, with texture cut-outs honoured. Decoded 3D props do not
  yet block these lights, and glass dims none of them — a pane blocks only at its opaque paint.
  Setting the count to nought turns every shadow off, pinned lights included.
- **`lit`** — whether the **thing itself is drawn lit**, the way the map's own lights are. Say
  nothing and it is: that is what a grant has always done, and what stops a torch reading as a dark
  sprite with a halo round it. `"lit": false` keeps the ordinary shading law on the sprite while the
  light it gives is untouched — the thing is then lit by the lights around it, its own among them,
  so it glows with its own light rather than being painted bright. A field of `sprites` and
  `objects` alone: textures and faces grant surfaces, and a placed light is drawn by nothing, so
  there is no sprite for it to speak about there. On a thing the map itself marks (`Lighting` 0)
  the mark is the map's own statement and this field does not unmark it.
- **`off`** — the whole entry rather than a field: this is not a light, whatever anything else said.

The **most specific statement anybody made is the one in force**, and the tables compose **field by
field** — so one object may be made stronger than the group it shares an ID with without restating
the colour its kind already carries:

> blanket (`sprites`, `textures`) → `"id"` group → `"index"` one thing

`off` follows the same ranking, whole: turning one object off leaves every other object of its kind
and its group alight, and naming one object out of a kind that is `off` makes that one a light again.

`objects`, `faces` and `placed` are written **under a map** and never under `everywhere` — a number
in one map's own list, and three numbers inside one building, name nothing anywhere else. `sprites`
and `textures` may be written in either: a sprite is the same sprite and a texture the same texture
wherever the game draws it.

#### Naming things: `sprites` and `objects`

`sprites` is keyed by the pair the editor's **object panel** shows for a selected object — its
**Texture Index**, an oblique, its **Texture Source**. `"34/0"` is Texture Index 34, Texture Source 0,
which on the first study map is the lit torch; `"112/0"` is an arcane wall ward the map does *not*
light, and `"93/0"` the empty candelabra that is plainly not a light at all. It answers for every
object of that kind in the game.

**The same picture has a second number, and both are accepted.** The editor's picture-picking window
shows one number per picture rather than a pair: that number is the picture's own place in the archive
it lives in. A map's own archive holds its object pictures from **4096**; the shared archive holds its
own from **0**. So

| Texture Source | picture number |
|---|---|
| 0 | `4096 + Texture Index` |
| 1 | `4352 + Texture Index` |
| 2 | `0 + Texture Index` |
| 3 | `256 + Texture Index` |

and `"112/0"` and `"4208"` are one key naming one picture. Write whichever number is in front of you.
A picture number in `4096`–`4607` belongs to **the map's own archive**, so which picture it is depends
on the map you are looking at — exactly as the pair does, since the pair is read against the same
archive. Numbers in `0`–`511` are the shared archive's and mean the same picture on every map of the
game. Anything between the two ranges is a *world texture* rather than an object's picture and is
refused with a note.

`objects` names one of them, or a group, the way the editor names it. Two ways, and an entry uses
exactly one:

- **`"index": N`** — the object's own number, which the editor shows as **`Object: N`** when you
  select it. It names exactly one thing.
- **`"id": N`** — the number the editor shows as **`ID:`**, which the map's creator typed to gather
  objects into a group for the command system. It is *meant* to be shared, so an entry keyed by it
  applies to **every object with that ID** — which is how a whole set of wards gets tuned in a
  single line. `"id": 0` is refused and says so: 0 is the default that most objects carry.

An entry that writes both, or neither, is refused with a note and the rest of the file is read.

**Numbers move when a map is edited.** The object number is a position in the map's own list, so
deleting an object in the editor shifts the number of every object after it. For a map that is
finished, `index` is exact and stable. For a map still being built, the **ID is the stable handle**:
it travels with the object it was typed on.

#### Naming geometry: `textures` and `faces`

`textures` says what a **painted surface** gives off — lava, a glowing rune, a lit sigil on a wall —
keyed by the texture number the map's own geometry carries. It covers **every face and every flat**:
a wall's three bands, a room's floor and ceiling, and the two faces of a block standing in a room.

Two things about it are worth knowing before you write one:

- **The colour comes off the texture's own bright part where it is not written.** A wall is not a
  cut-out the way a sprite is — every texel of it is the surface — so a plain average over lava
  returns the colour of the rock the lava is set in. The law here is therefore the picture's **bright
  part**: every texel at least half as bright as the brightest texel in it, weighted by brightness.
  Read that way lava throws its glow, a cold rune throws its line, and a surface painted one flat
  colour throws that colour. Write a `colour` and that is what is used instead; a texture whose
  picture cannot be read at all lights nothing and says so.
- **A texture is not a sprite.** This table is keyed by what a **surface** is painted with; `sprites`
  is keyed by the picture an **object** is drawn with. A pair like `"112/0"` written here, or a
  picture number like `"4208"`, names nothing any wall can be painted with — and the game says which
  table the line belongs in rather than leaving it quiet.
- **A grant lights the room; it does not repaint the surface.** Whether a face is drawn fullbright
  stays what the map itself says and what the game's own colours do with it. This adds a light beside
  the lava, not a new way of drawing lava.
- **A surface that changes while you play is followed.** Maps repaint walls and floors as they run —
  a lever changes a panel, a surface steps through a set of pictures — and the lights follow that: a
  face that takes up a granted texture lights from then on, and one that is repainted away from it
  goes out. A surface that flips between a granted picture and an ungranted one pulses with its own
  animation, which is the map doing exactly what it was authored to do. A floor carried up by a lift
  takes its light with it.

One light is placed **per room per texture**, in the middle of that room's granted faces and just
clear of them, however many faces there are: a lake of lava is one light over the lake rather than
three hundred lights fighting for the handful of slots the picture carries.

The `"100"` above is a real number too: it is the texture the sigil room's walls of the first study
map are painted with, so that line is one you can write, run and see — every room carrying it picks
up a green light. Put your own game's lava number in its place.

`faces` names **one face** instead, and it is the same pair again, because the editor shows the same
pair for a face:

- **`"index": N`** — the face's own number, which the editor shows as **`Face: N`** when you select a
  wall. It names exactly one face.
- **`"id": N`** — the read-only **`Face ID`** the editor shows beside it, which is the handle the
  map's command system addresses a face by. One texture-mapping record may be shared by several
  faces, so this names a **group** of them. `"id": 0` is refused: it is what a face with none of its
  own reads as.

A named face keeps a light of its **own**, at the middle of that face pushed just clear of it, rather
than being averaged into the middle of the room with the rest — the most specific statement wins here
too. A face named `off` is taken out of its room's blanket as well. And what a named face says is
laid over whatever `textures` already said about the texture it is painted with, so a face may be
made stronger without restating the colour its texture carries.

A face has no `Lighting` field of its own, so a named face always emits. Where nothing in that
composition states a `colour`, one is read off the texture the face is actually painted with, by the
same bright-part law a texture grant uses.

#### Reading the numbers off a map

Running the game with **`HWR_OBJ_DUMP`** set to a file name writes out every object the map **draws**,
with **its number in the first column**, then where it stands, then the sprite pair it is drawn with
and its `ID` — all decimal, all the numbers the editor shows for the same thing. After those come
the numbers of the records the map holds and never draws: an entry naming one of those can never
light anything, and the game says so by name rather than pretending the object is not there.

**The positions in that dump are the game's own**, and they are what `placed` is written in — so a
line copied out of the dump hangs a light exactly where the dump found the thing beside it.
**The ROTH Editor displays X with the opposite sign**, so an X read off the editor's position box has
to have its sign flipped before it is written here. Y and Z agree between the two.

#### When an entry does nothing, it says so

Each of these is said **once per map**, on the error stream, the same place the settings file's own
notes go:

- an entry naming a **hidden** record — *"object 273 on STUDY1 is hidden and never drawn; it cannot be
  a light"* — rather than claiming the map has no such object;
- a number past the end of the map's own list — *"STUDY1 numbers no object 900; its objects are
  numbered 0 to 273"*;
- an `ID` nothing on the map carries, for objects and for faces alike;
- a face number the map does not have;
- a `sprites` key written **under a map** that no object drawn there uses, named in both spellings —
  *"nothing on STUDY1 is drawn with the sprite 200/0, which is picture 4296"*;
- a `textures` key written **under a map** that no face or flat of that map is painted with;
- a `textures` key that is really a sprite — a pair, or a picture number — with the table it belongs
  in named;
- and a member of a block **given more than once**, naming it: *"'sprites' is given more than once
  here; only the last one counts, and everything the earlier one said is thrown away"*. JSON keeps the
  last of two members of one name, which is the right answer and a silent one — pasting four blocks
  together and losing two of them looks exactly like a file that was never written.

A `sprites` or `textures` key written in `everywhere` stays quiet on maps that do not use it, because
a game-wide entry is meant to name nothing on most of a game's maps.

### Lights the map never had: `placed`

`placed`, under the same `lights`, is for the other case entirely: a light that belongs to **no
object at all**, hung at three numbers you choose because there is nothing standing there to tune.

```json
"placed": [
  { "notes": "a lamp over the stairwell",
    "at": [1284, -2210, 96], "colour": "#ffb46e",
    "strength": 1.2, "reach": 420 }
]
```

- **`at` is required** and is where the light hangs, **in the game's own coordinates** — the same
  three numbers `HWR_OBJ_DUMP` prints for the objects around it, which is the easiest way to find a
  sensible one. If you take a position off the **ROTH Editor's** position boxes instead, **flip the
  sign of X**: the editor displays it negated. Y and Z agree.
- **`colour` is required**, and this is the one table where it is, because it is the one table with
  no picture behind it. A torch has a sprite the game can take a colour from and lava has a texture;
  a light hanging in the air has neither. An entry without a colour lights nothing and says so.
- `strength` and `reach` are optional and fall to the settings screen's own values, so you can hang a
  light and tune the whole rig afterwards.
- There is **no `off`**. An entry here exists only because you wrote it, so the way to be rid of one
  is to delete the line.

Each light is placed in whichever of the map's rooms contains the point — the same question the
editor asks when you drop an object into a room — and is then held by exactly the containments every
other light is held by. A light hung outside every room lights nothing and says so.

A `placed` list is written **under a map** and never under `everywhere`: the same three numbers in
another map are a different corner of a different building. **Light from things the map marks** and
**Light from glowing textures** do not reach these — a light you wrote down is not a kind of thing the
game has, so there is no separate switch for it — but **Light from torches and candles** turns them
off with everything else.

### The sky's own light: `environment_lighting`

`environment_lighting` sits beside `settings` and `lights` rather than inside either, because it
describes the one light in the game that is **not in the room with you**. It comes down out of the
sky, enters wherever the map is open to it, and everything between it and the floor — roofs, walls,
window bars, whatever is standing outside — casts a shadow into the room. A map with no opening to
the sky is untouched by all of it and needs no entry at all.

```json
"everywhere": {
  "environment_lighting": {
    "colour": "#ffede0", "strength": 0.8, "elevation": 45
  }
},
"maps": {
  "TOWER1": {
    "notes": "Night, high up. Moonlight through the north openings.",
    "environment_lighting": {
      "kind": "moon", "colour": "#b9c8ff", "strength": 0.35,
      "elevation": 22, "direction": 210, "shadow_quality": "high"
    }
  },
  "CRYPT1": { "environment_lighting": { "on": false } }
}
```

| field | absent means | what it says |
|---|---|---|
| `on` | **automatic**: lit wherever the map is open to the sky | holds the light on, or out of a map that has openings and should not be lit through them |
| `kind` | `"sun"` | `"sun"` or `"moon"` — **a label**, and nothing else: it decides which word the light is called by, while the colour and the strength are what it actually looks like |
| `colour` | the game-wide answer | what colour the light from the sky is |
| `strength` | the game-wide answer | how much of it there is |
| `elevation` | the game-wide answer | how high it stands above the horizon, **in degrees**: low means long shadows reaching deep through side windows, high means short ones under things |
| `direction` | the game-wide answer | which way it stands over the map, **in degrees** as a compass bearing — 0 north (the top of the map as the editor draws it), 90 east, 180 south, 270 west |
| `object_shadows` | the settings screen's own | whether things standing in the world cast into it |
| `shadow_quality` | the settings screen's own | `"low"`, `"medium"` or `"high"` |
| `sky_ceilings` | the game's own reading | which ceilings are open to the sky, for a map it reads wrongly — see below |

**`direction` is the per-map field.** These maps were built with no sun in them at all, so which of a
map's openings catch the light is nobody's truth and everybody's choice: stand in the room, turn the
**Sun compass direction** dial until the light falls where you want it, and write that number down
under the map. Everything else usually wants saying once, under `everywhere`.

**Every typed field above is also a settings key**, and the two are one statement written two ways:
`direction` is `sun_yaw_deg`, `colour` is `sun_color`, `strength` is `sun_intensity`, `elevation` is
`sun_elevation_deg`, `on` is `sun_light`, and the two shadow fields are `sun_object_shadows` and
`sun_shadow_quality`. They land in the same place and compose by the same rule, so the block is the
friendlier spelling rather than a second mechanism. Write a value **both** ways in one place and the
block is the answer in force, and the game says so once.

**`sky_ceilings` is the one place composition is not field by field.** Every other field of every
other table is laid over the answer above it on its own; a list is one answer rather than a bag of
fields, so **a map's `sky_ceilings` replaces the list above it** instead of adding to it — which is
what lets a map take a ceiling *out* of the reading as well as put one in. It is an escape hatch, and
most maps want no part of it: the game reads a ceiling as open sky when its texture number is 0 or 1,
which is how the maps of this game say it. Write the list only where that reading is wrong for one
map, write **every** ceiling that is open in it — including 0 and 1 if they still are — and an empty
list says that none of them is.

```json
"TOWER1": { "environment_lighting": { "sky_ceilings": [1, 2609] } }
```

Because these are the same rows the settings screen holds, a map that answers for any of them marks
them in red there with what the map says, and **Reload game config** puts an edited file in force
where you stand — turn `direction`, save, press the button, and the shadows swing round without a map
change.

### Documentation inside one: `notes`

JSON has no comments. A member named exactly **`notes`**, at any level, is documentation — a piece
of text or a list of them — and is never read as data:

```json
"STUDY1": {
  "notes": [
    "The study is lamplit and enclosed.",
    "Its own candles carry it, so the room reach comes down."
  ],
  "settings": { "room_light_reach": 0.72 }
}
```

`notes` is a reserved name: no part of a game config will ever use it for anything else. The shipped
`roth.json` is written almost entirely in `notes`, as a worked example to read and fill in — as it
ships it states no value and names no map, so it changes nothing until you write something into it.

### What a mistake costs

Two answers, and the difference is worth knowing:

- **A file that is not JSON costs the whole file.** One missing brace moves every following line
  into a different place, so half a file read would look like an answer. Nothing is loaded, whatever
  was in force stays in force, and the game reports where the file stopped making sense.
- **Anything else costs its own key.** An unknown key, a value out of range, a map name nothing
  matches, a colour that is not six hexadecimal digits: the game names that key, ignores it, and
  reads the rest of the file.

Both are reported on the error stream, the same place the settings file's own notes go.

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

**Modern** lights the world instead of shading it. The value a map stores for each of its rooms is
not a brightness — it is a distance, the range that room's own light reaches before it gives out —
and Modern reads it as exactly that, so a room lights its surfaces until its light runs out. A
corridor's far end goes dark while the wall in front of you does not; a room whose lights are on
stays bright down its whole length; the light you carry, a fill, and the map's own torches are
added to that. Rooms the map marks candlelit are lit by the light in your hand — that is the
game's own model, and Modern keeps it there rather than folding it into the room, so turning your
light down darkens exactly the rooms it is supposed to. Light switches, weapon flashes and everything else the map does to a room still
work, and read as light rather than as a jump from one shade to the next — a light switch moves the
room's own reach AND takes it out of the candle model, which is why a room reads properly lit when
it comes on rather than one step brighter.

At the reach of one and the candle at full, Modern draws the map exactly as it was authored, which
is the original's own picture with the thirty-two steps taken out of it. Every dial is a departure
from that picture rather than from a guess — and what Modern ships at is a departure on purpose: a
dark, candle-lit look, tuned in play — the room's light carrying to 0.85 of what the map states,
the candle at just under half the game's own, the flame lit and wandering — that reads as the dark
the game was written to be. **Original** puts the game's own picture back whole, and a game config
can put the map-as-written reach back on any map you want it on.

Modern shading brings its own settings with it, all of them dimmed until it is chosen:

- **How far a room's light carries** is the dial that decides what the world feels like. The value a
  map stores for each of its rooms is not a brightness: it is a distance, the range that room's own
  light reaches before it gives out. Read that way, a room lights its surfaces until its light runs
  out — so a corridor's far end goes dark while the wall in front of you does not, and a room whose
  lights are on stays bright down its whole length. At one the map is read exactly as written, which
  is the original's own picture; turning it down takes the dim rooms into darkness long before it
  touches the lit ones, which is what a single brightness dial cannot do because it shrinks the
  difference between them along with everything else. **Room brightness** is a master scale over the
  whole of it.
- **Never darker than** is the least any surface is ever left showing, as a share of the brightest
  the picture can be drawn: a hundredth is about two levels out of 255 and a tenth about
  twenty-five, so what you set is what you get whatever the texture underneath happens to be. It
  lifts what has no light and leaves what has alone, which is what keeps a dark room dark and still
  walkable — a floor that simply cut everything off at a level would paint every dark surface the
  same value, and a room with no differences in it is a room you cannot find your way across.
- **Carried light**, with its **colour**, **strength** and **reach**, is a real light where you are
  standing rather than a pool painted on the screen: surfaces facing you catch it, surfaces turned
  away do not, and it is correct in a mirror without being told anything about one. It is on by
  default under Modern, because a room's own light now runs out with distance and what is left when
  it does should be the light you brought. Its reach follows the map unless you set one, and its
  strength ships at a whisper on purpose: the candlelight's own cone is the picture's carried
  light, and near you the picture is already as bright as it can be drawn, so a stronger lamp does
  not brighten the near field, it flattens it. Raise it if you want a real modern lamp at the cost
  of some of that. **Flickers**, with its **depth** and **speed**, moves it the way the game's own
  flame does: the pool of light wanders about where you are looking — the depth is how far, full
  being the game's own throw, and the speed is the pace. It never strobes and never gutters out.
  **Carried light aims ahead** puts the lamp's pool a little way in front of you instead of on top
  of you. It ships at zero: the candle cone already aims the light where you are looking, so the
  lamp no longer needs an aim of its own to read as directed.
- **How much of a candlelit room it lights** is the other half of that light, and the half the game
  itself already had a word for. A room the map marks candlelit is drawn by the game at its own
  distance law less a fixed number of shade steps, and those steps are the candle rather than the
  room: the brightness around you in such a room IS the light in your hand. This is how much of it
  your light supplies. At full it is the game's own candle, so the picture is the one the game
  draws; turned down, candlelit rooms go dark around you, leaving only the light the room itself
  has — and just under half is where it ships, which is the tuned dark look above. The flicker
  moves the pool of it, so a flame you dial up and down is a flame you can see.
  **How soft your light's edge is** softens the cone's own rim: the game crosses from light to
  dark at one pace, and this spreads the crossing over a wider band about the same edge — a
  lantern's falloff instead of a flashlight's rim, with the middle of the cone and the darkness
  beyond it left where they are. Zero is the game's own edge.
  **Pool your light instead of filling the room** shapes it: the game's own
  candle is measured on the screen rather than in the world, so it brightens a whole candlelit room
  evenly however far away the wall is, and at nothing this is exactly that. Wound up, it gathers
  into a pool around you instead — bright at your feet, gone by the time your light runs out, and
  dimmer on surfaces facing away.
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
light rig may be set per map through a game config.
