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

## The two operations

Bumping the version and cutting a release are **separate**, and the changelog is what
keeps them apart:

- **Notes always land under `## [Unreleased]`** and stay there. They accumulate across
  however many version bumps happen in between.
- **Only a release stamps them**, moving the whole `[Unreleased]` section under a dated
  `## [X.Y.Z]` heading. That way a release lists every change since the previous one,
  no matter how many bumps it took.

`check-version.ps1` therefore does **not** require a dated section for the current
`VERSION` — it only checks that `VERSION` and `jump_version.h` agree and that an
`[Unreleased]` section exists.

## Day-to-day

1. When a change is player- or host-visible, add a bullet under `## [Unreleased]` in
   `CHANGELOG.md` (Added / Changed / Fixed / Removed as needed). Docs-only or
   agent-only work may skip a row.
2. Bump the version whenever you want a new build to identify itself — it is not tied
   to releasing:

   ```powershell
   ./scripts/bump-version.ps1 -VersionMode patch   # or minor | major
   ./scripts/bump-version.ps1 -Version 0.4.0       # or an exact version
   ```

   This touches `VERSION` and `jump_version.h` only, never `CHANGELOG.md`. Commit it
   as `chore: bump version to X.Y.Z`. Do **not** tag.

3. `build.bat` runs `scripts/check-version.ps1` before compiling. Skip with
   `Q2J_SKIP_VERSION_CHECK=1` if you must.

## Cutting a release

1. Make sure `[Unreleased]` has the notes that should ship.
2. From the repo root:

   ```powershell
   ./scripts/release.ps1                      # release whatever VERSION already says
   ./scripts/release.ps1 -VersionMode minor   # or bump on the way out
   ./scripts/release.ps1 -Version 0.4.0
   ```

   With no arguments it releases the version already in `VERSION`, which is the normal
   case since bumps happen during development. It stamps Unreleased into
   `## [X.Y.Z] - YYYY-MM-DD`, leaves a fresh empty Unreleased section, and re-runs the
   alignment check. It does **not** commit, tag, or push.

3. Finish with git:

   ```bat
   git add VERSION src/jump/jump_version.h CHANGELOG.md
   git commit -m "release: vX.Y.Z"
   git tag vX.Y.Z
   git push origin HEAD --tags
   ```

Tag names are always `vMAJOR.MINOR.PATCH` (e.g. `v0.1.2`).
