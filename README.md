# Realms of the Haunting (ROTH.C) mods

> **DISCLAIMER:** This project is still heavily under development. Mods are available for both Linux and Windows.

This project contains mods for Realms of the Haunting, when playing with the **[ROTH.C](https://github.com/beyond-the-ire/ROTH.C)** executable.

**ROTH.C** is a native recreation of the Realms of the Haunting engine, built with a thin modification layer allowing for any in-game function to be modified, extended, or even fully replaced.

The engine itself lives in a separate repository:
**[ROTH.C](https://github.com/beyond-the-ire/ROTH.C)**. You need a ROTH.C moddable build (and a
lawfully owned copy of the game's data files) to run any of these.

## The mods

| mod                             | what it does                                                                                     | for modders, it shows                                                                                                                               |
| ------------------------------- | ------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`keybinds`](keybinds/)         | Remap the game's action keys from a small text file, allowing you to build your own WASD layout. | A **data mod** plus the **scancode input seam**: rewriting the engine's keybind tables at boot and translating menu-navigation keys as input flows. |
| [`doc_viewer`](doc_viewer/)     | Enlarges the inventory "inspect document" window so you can view more of the document at once.   | A full **engine-function replacement** with a clean passthrough: replace a function's body, or fall through to the original when disabled.          |
| [`item_grabber`](item_grabber/) | An in-game item picker that can give any item in the game.                                       | The full **overlay toolkit**: a custom UI driven by the compose-tick, mouse-poll, and scancode seams, with engine calls behind it.                  |
| [`debug_overlay`](debug_overlay/) | Shows accurately named timing, location, map-geometry, and interaction-cursor diagnostics in game. | A passive **debug overlay** with cross-thread timing publication, validated read-only state sampling, and clipped palette-index drawing.        |

See each mod's `README.md` for screenshots and more information.

## Installing

A mod is a folder that goes next to the game.

0. Prerequisite: Make sure you have **[ROTH.C](https://github.com/beyond-the-ire/ROTH.C)** installed.
1. Download or build the mod folder (e.g. `keybinds/`) so it contains `plugin.so` (Linux) or
   `plugin.dll` (Windows) and, if the mod ships one, its `.cfg` file.
2. Copy that folder into the `mods/` directory beside the game executable (`roth` on Linux,
   `rothc.exe` on Windows) — the same directory the engine's release bundle drops an empty
   `mods/` into:

   ```
   <game>/mods/keybinds/plugin.so
   <game>/mods/keybinds/keybinds.cfg
   ```

3. Launch the game; the mod applies at startup.

To **remove** a mod, delete its folder from `mods/`. To verify what is installed, run the engine
with `--list-mods`: it prints every mod that would load (id, name, version) and exits without
starting the game. `--no-mods` boots with all mods inert.

## Building (developers)

Each mod compiles against the engine's public modding SDK with `gcc -m32` (32-bit support required; on Debian/Ubuntu that is the `gcc-multilib` package).

The simplest setup is to clone this repository next to a **[ROTH.C](https://github.com/beyond-the-ire/ROTH.C)** checkout:

```
some-directory/
├── ROTH.C/        # the engine (provides sdk/)
└── roth-mods/     # this repository
```

Then build a mod in its own directory:

```
cd keybinds
make            # -> plugin.so
```

The Makefiles find the SDK through `ROTH_SDK`, which defaults to a sibling `../../ROTH.C/sdk`. If
your engine checkout or unpacked SDK release lives elsewhere, point it there:

```
make ROTH_SDK=/path/to/ROTH.C/sdk
```

Copy the resulting `plugin.so` (and the `.cfg`, if any) into `<game>/mods/<mod>/` to install it.

### Building for Windows

The same Makefiles cross-build a Windows `plugin.dll` with the MinGW i686 toolchain by adding
`CROSS=mingw`:

```
cd keybinds
make CROSS=mingw   # -> plugin.dll  (needs the i686-w64-mingw32 toolchain)
```

Install a Windows `plugin.dll` exactly like the Linux `plugin.so`: drop the mod folder into
`<game>/mods/<mod>/` beside the game executable.

Developing on a Windows PC? Run the same builds inside
[WSL](https://learn.microsoft.com/windows/wsl/) (a full Linux environment; the instructions
above apply unchanged, and the built `plugin.dll` runs directly on the same machine). A native
MSYS2 build may also work since a mod is a single compiler invocation, but it is not validated.

On Windows each `plugin.dll` links its own copy of the MinGW C runtime, so a plugin's heap is
separate from the game executable's. The rule that already keeps the Linux plugins correct also
keeps them correct here: a plugin frees only what it allocated inside the plugin, and never frees
(nor asks the host to free) a pointer that came across the host API table. Engine and game memory
belong to the host and are reached through the SDK's bounded accessors — never `free()`d by a mod.

## Writing your own

Start with the SDK's **modding guide**, `sdk/MODDING_GUIDE.md`, in the
[ROTH.C](https://github.com/beyond-the-ire/ROTH.C) repository: it covers the mod types, the
execution-context rules, the override-chain semantics, and a build recipe.

Community mods are welcome and can be published anywhere; this repository is the home of the
first-party set.

## License

MIT, with one carve-out for the reconstructed engine function reused by `doc_viewer` — see
[`LICENSE`](LICENSE).
