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

## Elevator floor selectors (WARP_E1..WARP_E8)

The elevator family now follows the recovered Nitemare floor-selector path.

For each logical `WARP_E1..WARP_E8` class the generated XLAT records the
current raw wall ID plus the class-local minimum and maximum raw IDs. Runtime
then reproduces the original split between definition data and current-map
usage:

- the lowest raw ID comes from the WALLS class mapping,
- the highest usable floor is constrained by members actually present in the
  current 64x64 map,
- no more than ten floors are exposed,
- the selected floor resolves to `targetRaw - currentRaw`,
- that delta is passed through the same wall-target/free-neighbor relocation
  model used by the recovered `FUN_1010_2800` helper.

The original floor availability rule is also retained for normal gameplay.
When a MAP level is loaded, the native loader stores a two-bit static mask
describing whether raw IDCARD objects 0x09 (red) and 0x0A (yellow) were
originally present in that level. The elevator runtime compares this with the
player's current Nitemare ID-card inventory:

`unavailable = staticCardPresenceMask XOR playerCardMask`.

Floor menu entries selected by this mask remain visible but disabled and are
skipped by navigation, matching the original menu-record state 6 behavior.

The original `DAT_1048_4BE6` bypass is now identified as the Omnifarious
cheat. ECWolf does not yet provide the Nitemare cheat backend, so this bypass
is intentionally not exposed; default/non-cheat gameplay follows the original
availability rule.

The modal ECWolf presentation is functional rather than pixel-identical: it
labels disabled rows as `[locked]` and permits Escape to close the selector.
The transport/floor selection semantics are the fidelity target of this layer.

## Climb warps (WARP_1..WARP_8)

The Win16 climb family is now decoded and wired into the generated translator.

The original `FUN_1018_1EE0` builds a three-entry modal menu:

- command 0x1B: `Climb up`,
- command 0x1C: `Climb down`,
- command 0x19: `Cancel`.

The command dispatcher proves that 0x1B calls the shared passage helper with
`+1`, while 0x1C calls it with `-1`. Therefore the physical target is the
first current-map cell whose raw wall ID is the current warp ID plus or minus
one; the shared free-neighbor helper then performs the same N/E/S/W blocked-cell
search used by the colored-key passages.

Endpoint enable rules are also preserved:

- Down is disabled at the lowest raw ID defined for the logical WARP_n class.
- Up is disabled when the current raw ID is the highest member of that class
  actually used in the current map.

ECWolf now exposes this as `Nitemare_ClimbWarp` (special 16). A small modal
chooser keeps the current game/music context and offers only the enabled
directions plus Cancel. The generated per-episode XLAT passes each WARP_1..8
raw ID and its class bounds into the runtime special.

## Colored-key passages (WARP_L1..WARP_L4)

The executable audit now closes enough of the shared passage helper to model
the four reusable colored-key gates directly.

On successful USE the original helper:

1. checks the corresponding reusable colored key,
2. starts from the gate wall cell,
3. searches neighboring cells in N -> E -> S -> W order,
4. skips the player's current cell,
5. rejects a candidate when wall/object blocking bit 0x02 is set,
6. moves the player to the center of the first free candidate,
7. assigns the cardinal facing associated with that candidate.

The ECWolf special `Nitemare_KeyPassage` reproduces that structural behavior.
`WARP_L1..L4` map to Nitemare locks 201..204. Translated wall tiles represent
the original wall-blocking test and generated +SOLID actors represent the
verified blocking object range.

The original success SFX/event 0x32 is not bound yet because the native SND.DAT
event-name mapping remains separate audio work.

## Level gateways

The generated wall translator now handles the two verified level-gateway
families:

- `LEVEL_UP` -> ECWolf `Exit_Normal` on player USE,
- `LEVEL_UP2` -> a Nitemare-specific `Nitemare_LevelUp2` special.

`Nitemare_LevelUp2` is restricted to the Nitemare3D game family and resolves
the current MAPINFO `LevelNumber`, then transitions directly to
`LevelNumber + 2` through ECWolf's normal `ex_newmap` path. This preserves
the documented "skip a level" meaning without abusing secret-exit semantics.

## Colored keys and locked doors

The bootstrap now uses ECWolf's native Key/LOCKDEFS system for the six
verified Nitemare access items:

- red / green / blue / yellow key,
- red / yellow ID card.

OBJECTS IDs 0x05..0x0A are generated as Key-derived inventory actors with
always-pickup behavior. LOCKDEFS 201..206 accept the matching episode-specific
raw actor from IMG/OBJECTS.1, .2 or .3.

Colored locked sliding-door families `DOORVL/HL`, `DOORVL2/HL2` and
`DOORVL3/HL3` now emit `Door_Open` triggers with the appropriate lock.
The original editor catalogs vary wording between forms such as "red key",
"Locked Red Door" and "red - locked", so color detection is deliberately
restricted to these known locked-door classes.

Transportation Chamber `DOORVI/HI` and remote `DOORVR/HR` remain
unimplemented here because the original executable binds those families to
associated OBJECT subtype/group state rather than to wall color alone.

## Bootstrap sliding doors

The generated wall translator now promotes only the wall families whose
orientation and direct-use behavior are already safe to express structurally:

- `DOORV` and `DOORH`,
- `DOORVC` and `DOORHC` (curtain doors).

Vertical classes receive ECWolf's vertical slide offset and horizontal classes
receive the horizontal slide offset. A repeatable player-use `Door_Open`
trigger is emitted for those raw wall IDs.

The current ECWolf trigger uses bootstrap speed/hold parameters (16 / 300);
those values are engine-side placeholders, not claimed original Nitemare
timing. Locked `DOORVL/HL*`, Transportation Chamber `DOORVI/HI`, and remote
`DOORVR/HR` families deliberately remain closed/static until their verified
key/card/remote-control state logic is connected.

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
