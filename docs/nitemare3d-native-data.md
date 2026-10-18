# Native Nitemare 3D data support

This branch adds a first native data layer for Nitemare 3D without converting
the original game files on disk.

## Implemented containers

### IMG.1 / IMG.2 / IMG.3

The loader validates the original sequential IMG layout and exposes:

- a per-episode marker (NITIMG1..3),
- the original IMG header (N1IHDR, N2IHDR, N3IHDR),
- every physical image record as `graphics/NxI####.n3i`.

A dedicated ECWolf texture decoder understands the Nitemare image record:

- byte 0: width,
- byte 1: height,
- bytes 2..9: preserved metadata,
- byte 10 onward: width*height indexed pixels,
- pixel data is column-major and therefore maps directly to ECWolf's software
  texture layout.

Sprite transparency index 31 is supported when an IMG record is explicitly
placed in the sprite namespace.

The two 256-entry IMG directories are also resolved against exact physical
frame boundaries:

- wall resource ID XX -> `textures/NxWXX.n3i`,
- object resource ID XX -> first-frame sprite alias `sprites/NxXXA0.n3i`.

The object alias uses an ECWolf-compatible four-character sprite stem followed
by frame A / rotation 0. Only exact directory-to-frame matches are aliased;
sequence-bank timing and subsequent animation frames remain a separate layer.

### MAP.1 / MAP.2 / MAP.3

The loader validates:

- 514-byte archive header,
- declared level count,
- 8192 bytes per level,
- 64x64 cells,
- two bytes per cell: wall byte + object byte.

Each source level is converted in memory to ECWolf's existing WDC3.1/PLANES
format. The map markers are:

- episode 1: N1M01 ...
- episode 2: N2M01 ...
- episode 3: N3M01 ...

The converted PLANES lump contains a 16-bit wall plane, a 16-bit object plane,
and a zeroed third plane. This makes the original maps structurally readable by
ECWolf's existing GameMap::ReadPlanesData path.

Gameplay translation is deliberately separate. WALLS.*, OBJECTS.*, doors,
warps, triggers, player starts, enemies, weapons and specials still need a
Nitemare-specific xlat/game layer.

### WALLS.1 / WALLS.2 / WALLS.3 and OBJECTS.1 / OBJECTS.2 / OBJECTS.3

The original editor definition catalogs are now recognized as native Nitemare
resources. Each non-empty line is validated as:

- hexadecimal map ID (0x00..0xFF),
- visual code,
- image name,
- class name,
- optional free-form description.

The original table bytes are preserved. Per-episode markers and raw definition
lumps are exposed as:

- walls: `NITWAL1..3` + `N1WDEF..N3WDEF`,
- objects: `NITOBJ1..3` + `N1ODEF..N3ODEF`.

A bootstrap ECWolf translator is generated in memory for each episode:

- `N1WXLAT..N3WXLAT` translates physical wall IDs to `N?WXX` texture aliases,
- each wall translator includes its matching `N?OXLAT`,
- `FLOOR`, TURN/RETREAT and known invisible marker classes are kept open as
  zones rather than promoted to solid walls,
- `N1OXLAT..N3OXLAT` translates the verified object IDs 1..4 into the
  four-direction `$Player1Start` range.

This translator is intentionally structural. Doors, warps, exits, secret
panels, explodable walls, enemy classes, pickups and other executable-driven
semantics are not guessed here; until their runtime layer is wired, visible
non-floor wall classes remain ordinary solid tiles.

### SND.DAT

The DAT directory is read as six-byte descriptors:

- uint16 little-endian length,
- uint32 little-endian absolute offset.

Entries are exposed by original slot number.

- Standard MIDI entries -> music namespace.
- Creative VOC entries -> sound namespace, original VOC retained.
- IBK entries -> raw global lump.
- Other non-empty Windows Nitemare sound entries -> wrapped in-memory as
  unsigned 8-bit mono PCM WAV at 11025 Hz.

The original SND.DAT is not modified.

### UIF.DAT

Uses the same six-byte DAT descriptor format.

- PCX entries -> graphics namespace.
- MIDI/VOC entries -> music/sound namespace.
- unknown entries -> raw global lumps.

### ENDING.FLI

ENDING.FLI is validated as an 8-bit FLI/FLC-family stream and exposed as
`graphics/ENDINGFL.fli` plus the NITFLI marker.

Playback/decoding of FLI chunks is not wired into the game loop yet. The
existing reverse-engineered decoder in Nitemare3DDataEditor remains the
reference for COLOR_64/COLOR, LC, BLACK, BRUN and COPY chunk support.

### GAME.PAL

GAME.PAL is accepted when it contains the standard 256-color PCX palette
trailer. The loader exposes:

- the full PCX as `graphics/GAMEPAL.pcx`,
- the final 768 RGB palette bytes as NITPAL8.

This prepares native palette selection without baking a palette into IMG
conversion.

## Experimental game bundle

The engine can now collect a native Nitemare 3D installation as one game bundle
instead of treating the numbered files as unrelated Wolf-style extensions.

Required files for the current bootstrap are:

- MAP.1 / MAP.2 / MAP.3
- IMG.1 / IMG.2 / IMG.3
- WALLS.1 / WALLS.2 / WALLS.3
- OBJECTS.1 / OBJECTS.2 / OBJECTS.3

When present, SND.DAT, UIF.DAT, ENDING.FLI and GAME.PAL are added to the same
bundle automatically.

The bundle is registered in IWADINFO as `Nitemare 3D (Experimental)` with
the selector ID `nitemare3d`. The experimental label is explicit, but the
entry remains visible in the normal game picker so a Nitemare-only data
directory does not get filtered out as "no base game data".

`mapinfo/nitemare3d.txt` exposes all preserved maps:

- N1M01..N1M11,
- N2M01..N2M10,
- N3M01..N3M10.

N1M11 is kept isolated as the preserved Episode-1 demo map. Each episode uses
its matching generated translator (`N1WXLAT`, `N2WXLAT`, `N3WXLAT`).

A minimal `NitemarePlayer` PlayerPawn is provided only to bootstrap map
loading and movement. It uses the verified 100 HP and 27-unit collision radius
but intentionally has no Nitemare weapon/inventory implementation yet.

The bootstrap MAPINFO requests the native `NITPAL8` palette. Startup now
uses it whenever GAME.PAL supplied that lump and falls back only for the
Nitemare3D game family to ECWolf's built-in WOLFPAL when GAME.PAL is absent.
The fallback keeps the experimental bundle startable but is not palette parity.

## Source-of-truth repositories

The implementation was derived from the verified format work already kept in:

- marek177/Nitemare3d-reversed
- marek177/Nitemare3DDataEditor
- marek177/nitemare3d-img

The code intentionally does not promote still-uncertain animation, wall,
object, combat or special-wall semantics into engine behavior.

## Next integration layer

The next step toward a playable Nitemare 3D game definition is:

1. extend the generated bootstrap xlat with executable-verified doors, warps, exits, triggers and object classes,
2. expand the new IMG resource aliases into complete wall/object animation sequences,
3. replace the bootstrap player with the complete Nitemare player, inventory and weapon runtime,
4. map SND.DAT slot IDs into SNDINFO and music definitions,
5. wire ENDING.FLI playback,
6. add Nitemare actors, weapons, doors/warps, collision and special-wall
   behavior from the executable reverse-engineering results.

This resource layer is intentionally usable before those gameplay semantics are
complete.

## DOS/Windows compatibility audit (18 October 2026)

Audited baseline: PR #6 head `c6662da749477798a2ae30a05cabc362e459274e`.
Evidence: [RE v0.41 section 9](https://github.com/marek177/Nitemare3d-reversed/blob/main/docs/V041_REMAX_LATEST_STATIC_CONSOLIDATION.md).
This is a source audit; original commercial archives were not available in this
checkout and were not added or executed.

| Status | Finding | Scope |
| --- | --- | --- |
| Confirmed in v0.41 report | E1M1 OBJECT differs in 20 bytes; 33 DOS / 39 Windows initial GUARD placements | Only the report's fingerprinted `nitemare.zip` pair; not a detector for all DOS/Windows versions |
| Confirmed in v0.41 report | Audited IMG alias partitions match despite absolute offset differences | Six audited banks; not proof that arbitrary IMG files are interchangeable |
| Confirmed in code | MAP conversion copies each wall/object byte to its original cell in separate 16-bit planes | `FNitemareMapLump::FillCache`, shared `NitemareLayout::ExpandCells`; no hardcoded enemy count or sorting |
| Confirmed in code | IMG aliases resolve ID directory offsets against that IMG's physical frame boundaries | `OpenImg`, shared `FindFrame`; physical frame ordinals are local, not universal IDs |
| Confirmed in code | Bundle discovery gathers twelve named files from one directory; translators include their matching episode | `LookForNitemareData`, `BuildDefinitionXlat`, episode MAPINFO |
| Unknown | Arbitrarily selected MAP/IMG/definition files share historical distribution provenance | Names, valid structure, same directory and alias topology cannot establish this |

The retained OBJECT plane preserves the differing E1M1 placements as input data.
**The current bootstrap only translates player starts, not GUARD actors.** This
change therefore does not claim 33/39 enemies spawn in playable ECWolf gameplay.
It also does not claim original constructor ordering, enemy runtime, sequence
bank semantics or parity. Object classifications still need executable evidence.
Physical frame `NxI####` names are enumeration aids; game definitions must use
local ID aliases and must not transplant physical ordinals between distributions.

### Exact bundle compatibility check

Use `tools/nitemare_bundle.py` before starting the engine. It binds MAP, IMG,
WALLS and OBJECTS for **all three episodes together**, including present optional
files, by filename role, byte length and SHA-256. A changed MAP with unchanged
IMG is rejected just as a changed IMG, changed catalog or swapped episode is.
Case-insensitive duplicate names and missing required roles are rejected.
There is no shipped allowlist of historical hashes: the v0.41 summary alone does
not provide enough information to construct one reliably.

After independently verifying the origin of a complete local distribution:

```
python3 tools/nitemare_bundle.py /path/to/data --snapshot /path/to/dos-reference.json --label "independently verified source and version"
```

Keep separate reference manifests for each verified distribution. Snapshot is an
explicit attestation of the selected bytes, **not automatic DOS/Windows detection**.
It refuses to overwrite an existing manifest. To check a later installation:

```
python3 tools/nitemare_bundle.py /path/to/data --verify /path/to/dos-reference.json
```

Use the successful exit status as a launch prerequisite, and launch ECWolf with
that same data directory. Do not regenerate a reference merely to silence a
mismatch. Manifests contain hashes and sizes, no original asset bytes. Labels are
informational and cannot override mismatching hashes. Optional-file additions or
removals also require deliberate review of the reference.

**Enforcement boundary:** this is an explicit preflight tool, not an in-engine
manifest verifier. Direct engine startup still permits unverified installations
and now prints a provenance warning during native bundle discovery. Overrides or
mods loaded after verification are outside this check, and concurrent file edits
between verification and engine load are not prevented. Unknown historical sets
must remain described as unverified rather than being rejected based on the
33/39 count or accepted based on matching alias partitions.

### Regressions without original assets

`tests/nitemare_layout_test.cpp` exercises the same map expansion and exact
boundary lookup helpers used by the engine: all 256 byte values, original cell
order, zero extension, a synthetic 20-byte OBJECT-only difference, counts 0..120
(including 33 and 39), shifted physical offsets and repeated/reordered aliases.
Synthetic IDs are not claims about historical GUARD classes or coordinates.
`tests/test_nitemare_bundle.py` tests all twelve bound roles, episode swaps,
missing files, incomplete manifests, case collisions and optional changes.
The GitHub Actions workflow runs both without commercial assets.

Local validation: standalone C++98 regression and six Python unittest cases pass;
`git diff --check` passes. Full engine configuration/build was not run because
`cmake` is unavailable in the execution environment. These tests verify input
preservation and reference-set checking, not a real-data or gameplay parity run.
