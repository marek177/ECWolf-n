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

## Nitemare door adapter (timing unverified)

Ordinary `DOORV/DOORH` and curtain `DOORVC/DOORHC` retain their verified
visual aliases, slide axis and repeatable directional player-use trigger.
The trigger now calls `Nitemare_DoorUse` (arg0: 1 vertical, 2 horizontal).
The former `Door_Open` speed/hold placeholders 16/300 have been removed.

**Current behavior:** the adapter rejects activation and leaves the door
closed/static, with a diagnostic. This intentionally removes bootstrap map
traversability until the conversion is established. No thinker, slide amount,
zone link or sound is changed; no clock conversion is guessed. Locked,
transportation and remote families remain static as before.

### Evidence and representability

Source: [Nitemare3d-reversed PR #31](https://github.com/marek177/Nitemare3d-reversed/pull/31),
commit `89d6c852664f2e3d64753c824cc5d79a4b894d12`, specifically
`docs/REMOTE_DOORS_AND_DOS_CROSSCHECK_2026-09-22.md` and
`docs/WIN16_DOOR_RUNTIME_LAYOUT_CLOSURE_2026-09-28.md`.
These are static findings for **Win16 1.10**, not demonstrated DOS parity.

| Rule | Adapter representation | ECWolf generic Door_Open limitation |
|---|---|---|
| States 0 open, 1 closed, 2 opening, 3 closing, 4 corpse hold-open | Explicit enum; corpse events are inert | EVDoor has four states and no permanent corpse state |
| Motion 2 internal units/update | Original-unit constant only | Constructor scales speed by 64; world-unit and update conversion unknown |
| Completion countdown 32 | Discrete completion event for opening/closing | Generic thinker holds only after opening, destroys on close |
| Obstructed auto-close retry 4 | Discrete retry event at open/countdown zero | Generic thinker resets wait to full opentics |
| SFX 0x25/0x26 and latch +0x14 | Explicit request IDs; opening retains latch, closing clears it | Generic sound sequences have no original latch semantics |

`src/nitemare_door_adapter.h` represents those discrete events independently
of ECWolf's scheduler. It is not a running door simulation or a binary record
layout. No endpoint, collision criterion, timer decrement order, USE reversal,
corpse detection or SND resource mapping is inferred. Original sound requests
are not yet playback calls. The native data bundle does not establish which
executable version produced the files.

**Unknown:** exact simulation time, world-unit projection to slide geometry,
original obstruction/corpse detection integration and build-specific parity.
Before enabling the runtime, capture Win16 1.10 traces of completed opening,
closing, obstructed expiry and corpse hold-open; measure update cadence and
wall displacement, then verify their mapping to ECWolf ticks and geometry.

### Regression checks

Run from the repository root:

```sh
c++ -std=c++98 -Wall -Wextra -Werror -I src tests/nitemare_door_adapter_test.cpp -o /tmp/nitemare-door-test
/tmp/nitemare-door-test
python3 tests/check_nitemare_door_integration.py
```

The executable checks discrete events, latch retention/consumption, corpse
inertness and the disabled activation gate. Integration guards check generated
special wiring and prevent delegation to EVDoor or bootstrap values. These
checks do not establish original-runtime timing or full-engine build success.

## Generated bootstrap object actors

OBJECTS.1-3 now generate a DECORATE lump at load time for object classes whose
runtime class mapping is already established by the executable audit.

Each translated raw object ID gets an episode-specific actor class
(`N3DE1Oxx`, `N3DE2Oxx`, `N3DE3Oxx`) whose Spawn state points at the
matching first-frame IMG sprite alias `N?xxA0`. The generated OXLAT maps the
original object byte directly to that actor.

The current generated actor layer covers the documented GUARD families plus
CAUSTIC, SAFE, TRUNK, PUSH, ACTION, PERMEABLE, DUMB, ELEVATED, KEY, IDCARD,
FOOD, WEAPON, AMMO, CRYSTALB, MAGICEYE, PENTAGRAM and SCROLL classes when
those names are present in the episode definition table.

Collision uses one directly recovered property rule: mapped object classes
0x08..0x2D receive a 32-unit-radius solid placeholder. Classes outside that
verified blocking range remain non-solid. This gets the object plane into the
map and preserves the original broad blocking/non-blocking split, but it does
not yet implement GUARD AI, pickups, push movement, safe/trunk interaction,
caustic damage or animation sequences.

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
3. replace generated placeholder actors with the recovered GUARD/pickup/push/special-object runtimes,
4. replace the bootstrap player with the complete Nitemare player, inventory and weapon runtime,
5. map SND.DAT slot IDs into SNDINFO and music definitions,
6. wire ENDING.FLI playback,
7. complete doors/warps, collision and special-wall behavior from the executable reverse-engineering results.

This resource layer is intentionally usable before those gameplay semantics are
complete.
