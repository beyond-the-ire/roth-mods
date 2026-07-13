# Keybinds

A simple config-based keymapper for **[ROTH.C](https://github.com/beyond-the-ire/ROTH.C)**.

## What it does

Keybinds rewrites both of the engine's keybind tables so you can remap the keys as you see fit:

The two keybind tables are:

- the one-shot **PRESS** actions (fire, use, inventory, quicksave, the weapon slots, the F-key toggles, ...)
- the **HELD** movement/look actions (forward/back, strafe, turn, jump, crouch, look up/down)

It can also translate keys for in-menu navigation (the menus read the arrow keys directly), letting
you drive menus with keys like W/S — but that part is off by default (see below).

## Install

Extract the mod so its folder lands in the game's `mods/` directory:

```
<game>/mods/keybinds/plugin.so
<game>/mods/keybinds/keybinds.cfg
```

Both files must sit together in `mods/keybinds/`. Launch the game and the rebinds apply at startup.

## Configuration

All binds live in `keybinds.cfg`, one per line:

```
action = key
```

`#` starts a comment. Unknown action or key names print a warning and are skipped. With no cfg file
(or an empty one) the game keeps its stock binds.

Binding is **swap-or-set within the action's own table**: a PRESS action can only take a PRESS key
and a HELD action only a HELD key. If the key you assign already carries another action in that same
table, the two actions **swap** keys; otherwise the action just moves to the new key.

### Actions

See the `keybinds.cfg` or `keybinds.c` for the full list, but here are some of the current actions
that can be remapped.

**HELD** (movement/look, active while the key is down):
`move_forward` `move_backward` `strafe_left` `strafe_right` `turn_left` `turn_right` `jump`
`crouch` `look_up` `look_down` `look_recenter_up` `look_recenter_down`

**PRESS** (one-shot):
`fire` `use_enter` `inventory` `escape_menu` `weapon_1`…`weapon_6` `sprint_toggle` `quicksave`
`quickload` `show_version` `weapon_overlay` `subtitles` `easy_select` `mouse_swap` `display_cycle`
`display_cycle_alt` `gamma_up` `gamma_down` `snapshot` `view_scale_0` `view_smaller` `view_larger`
`flush_textures` `wireframe_map`(dev mode only) `render_mode` `help_overlay` `choice_up` `choice_down`

### Key names

`a`–`z`, `0`–`9`, `f1`–`f10`, `esc`, `tab`, `enter`, `space`, `ctrl`, `alt`, `lshift`, `rshift`,
`capslock`, `backspace`, `minus`, `equals`, `lbracket`, `rbracket`, `semicolon`, `quote`, `grave`,
`backslash`, `comma`, `period`, `slash`, `up`, `down`, `left`, `right`, `kp0`–`kp9` — or a raw
scancode written as `0xNN`.

### Example: a WASD layout

```
# WASD movement + Q/E turning + Space to jump
jump          = space
move_forward  = w
move_backward = s
strafe_left   = a
strafe_right  = d
turn_left     = q
turn_right    = e

```

### Menu navigation (optional)

The four `menu_up` / `menu_down` / `menu_left` / `menu_right` binds are special: while a menu owns
input, the chosen key is translated to the arrow key the menus expect, and during normal play it
passes straight through. They are off by default because that translation also fires inside the
save-name text field. The arrow keys always navigate menus regardless.

### Optional environment overrides

- `ROTH_KEYBINDS=/path/to/file.cfg` — use a config file from somewhere else
- `ROTH_KEYBINDS_DUMP=1` — print both live keybind tables at startup
- `ROTH_KEYBINDS_DEBUG=1` — log each in-menu key translation

## Disabling / removing

- Empty or delete `keybinds.cfg` to keep the stock binds (the mod becomes a no-op).
- Delete the `mods/keybinds/` folder to remove the mod entirely.
- Launch with `--no-mods` (or set `ROTH_MODS=0`) to disable all mods at once.
