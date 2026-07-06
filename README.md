# Quake II Remastered (KEX) Mod Template

A clean, buildable starting point for **Quake II Remastered** (the 2023 KEX
re-release) game-DLL mods. Fork or copy this repo, rename it, and start adding
your own gameplay code — the project already compiles to a drop-in
`game_x64.dll`.

The design goal is a **thin vanilla** layout: keep the upstream re-release
source untouched as much as possible and put your mod logic in its own folder.
See [`docs/THIN_VANILLA_PRINCIPLES.md`](docs/THIN_VANILLA_PRINCIPLES.md).

---

## Prerequisites

- **Windows** with **Visual Studio 2022** (any edition — Community works),
  including the **Desktop development with C++** workload (MSVC v143 toolset).
- **Quake II Remastered** installed (Steam/GOG) if you want to test in-game.
- Git.

## First-time setup

The build depends on [vcpkg](https://github.com/microsoft/vcpkg) in manifest
mode (`src/vcpkg.json` pulls in `fmt` and `jsoncpp`). Bootstrap it once at the
repo root — it is intentionally **not** committed (see `.gitignore`):

```bat
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg.exe integrate install
```

`integrate install` wires vcpkg into MSBuild so the manifest dependencies are
restored automatically on the next build.

## Building

From the `src` folder:

```bat
build.bat
```

`build.bat` will:

1. Locate VS 2022 automatically via `vswhere` (no hard-coded paths).
2. Build `game.sln` in **Release|x64** → `dist/game_x64.dll`.
3. If a Quake II install is found, copy the DLL into it and launch a test map.

To point the deploy/launch step at your install without editing the script:

```bat
set "Q2_DIR=D:\Games\Quake 2\rerelease" && build.bat
```

If `Q2_DIR` isn't found, the build still succeeds and just skips the
copy/launch step. You can also build straight from Visual Studio by opening
`src/game.sln`.

## Installing the mod manually

Copy `dist/game_x64.dll` into your Quake II install's game folder, e.g.:

```
<Quake II>\rerelease\baseq2\game_x64.dll
```

(or a mod subfolder loaded with `+set game <mod>`).

---

## Project layout

```
.
├─ README.md
├─ LICENSE                     # GPL-2.0 (matches the re-release source headers)
├─ .gitignore / .gitattributes
├─ docs/
│  └─ THIN_VANILLA_PRINCIPLES.md   # how to extend without forking upstream
├─ dist/                       # build output (git-ignored)
└─ src/
   ├─ game.sln / game.vcxproj  # MSBuild project (Release|x64 → dll)
   ├─ vcpkg.json               # manifest dependencies
   ├─ build.bat                # one-shot build + deploy + launch
   ├─ .vscode/tasks.json       # VS Code build tasks
   ├─ g_*.cpp / p_*.cpp        # upstream game logic
   ├─ m_*.cpp                  # monster logic
   ├─ cg_*.cpp                 # client game
   ├─ bots/                    # bot support
   ├─ ctf/  rogue/  xatrix/    # mission-pack / mode code
   └─ <your-mod>/              # ← add your mod folder here
```

## Using this as a template

1. Copy the folder (or "Use this template" if hosted on GitHub) and rename it.
2. Create your mod folder under `src/` (e.g. `src/mymod/`) and add its `.cpp`
   files to `game.vcxproj` (and `game.vcxproj.filters`).
3. Keep edits to upstream `g_*/p_*/m_*` files small and tag them, per
   [the thin-vanilla principles](docs/THIN_VANILLA_PRINCIPLES.md).
4. Build and test.

## License

The game source is **Copyright © ZeniMax Media Inc.** and licensed under the
**GNU General Public License v2.0** — see [`LICENSE`](LICENSE). Any code you add
here is likewise bound by the GPL-2.0 terms. This is an unofficial,
fan-made modding base and is not affiliated with or endorsed by id Software or
ZeniMax Media.
