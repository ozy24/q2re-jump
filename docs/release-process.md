# Release process

q2re-jump uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`.
The project is pre-1.0, so breaking changes are allowed on **minor** bumps.

## Sources of truth

| File | Role |
|---|---|
| [`VERSION`](../VERSION) | One-line release version (`0.1.2`) |
| [`src/jump/jump_version.h`](../src/jump/jump_version.h) | Compile-time macros; must match `VERSION` |
| [`CHANGELOG.md`](../CHANGELOG.md) | Player-facing notes (Keep a Changelog) |

`GAMEVERSION` in `src/g_local.h` stays `baseq2` — that is the engine gamename, not
the mod version. The running server exposes the mod version as the read-only
`jump_version` cvar.

## Day-to-day

1. Leave `VERSION` / `jump_version.h` alone between releases.
2. When a change is player- or host-visible, add a bullet under
   `## [Unreleased]` in `CHANGELOG.md` (Added / Changed / Fixed / Removed as
   needed). Docs-only or agent-only work may skip a row.
3. `build.bat` runs `scripts/check-version.ps1` before compiling. Skip with
   `Q2J_SKIP_VERSION_CHECK=1` if you must.

## Cutting a release

1. Make sure `[Unreleased]` has the notes that should ship.
2. From the repo root:

   ```powershell
   ./scripts/release.ps1 -VersionMode patch   # or minor | major
   # or an exact version:
   ./scripts/release.ps1 -Version 0.2.0
   ```

   The script updates `VERSION` and `jump_version.h`, stamps Unreleased into
   `## [X.Y.Z] - YYYY-MM-DD`, leaves a fresh empty Unreleased section, and
   re-runs the alignment check. It does **not** commit, tag, or push.

3. Finish with git:

   ```bat
   git add VERSION src/jump/jump_version.h CHANGELOG.md
   git commit -m "release: vX.Y.Z"
   git tag vX.Y.Z
   git push origin HEAD --tags
   ```

Tag names are always `vMAJOR.MINOR.PATCH` (e.g. `v0.1.2`).
