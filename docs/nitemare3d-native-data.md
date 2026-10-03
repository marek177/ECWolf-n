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

## Projected-row player damage seed

The player-to-GUARD damage base no longer uses the temporary generic 1..64
bootstrap source.

The recovered original formula is:

`seed = 8 * (OBJECT.last_projected_y_cache - viewport_center_y) + RNG % 25`.

The projection audit also establishes that the cached row is the viewport
center plus a perspective scale term. Nitemare's renderer derives sprite
height as:

`projectedSpriteHeight = sourceImageHeight * projectionScale / 32`.

The ECWolf bridge therefore derives the equivalent Nitemare projection scale
from the already-cached actor projection:

`projectionScale = projectedSpriteHeight * 32 / sourceImageHeight`.

`R_NitemareProjectedDamageScale()` uses the actual current sprite texture,
ECWolf cached `actor->viewheight`, actor Y scale, and texture scaling to
recover that term. The common raw damage seed is then:

`rawDamage = projectionScale * 8 + random()%25`.

The two hit routes intentionally differ in cache freshness:

- Silver Pistol targeting first refreshes `TransformActor()`, matching the
  original current-render-generation requirement before reading the damage
  cache.
- Projectile collision does **not** refresh the target projection. It consumes
  the actor's existing cached `viewheight`; if the actor has never acquired a
  valid projection, the raw damage path yields zero. This preserves the
  original stale-cache architecture rather than silently recomputing damage
  geometry on impact.

The exact fixed-point projection constants and historical slot-reuse artifacts
of the 16-bit OBJECT pool cannot be byte-identical inside ECWolf's different
renderer, but the recovered dependency/order is now represented: perspective
projection cache -> x8 + RNG%25 -> class/weapon transform -> difficulty ->
GUARD HP.

## Player projectile GUARD collision

The bootstrap projectile weapons now use a Nitemare-specific GUARD collision
branch inside ECWolf's projectile thinker while leaving every non-Nitemare
projectile on the existing ECWolf collision path.

For actors derived from `NitemarePlayerProjectile`:

- GUARD collision uses the recovered axis-aligned test
  `abs(dx) <= 9 && abs(dy) <= 9` Nitemare world units,
- the 9-unit tolerance is converted using the 64-units-per-tile world scale,
- a successful proximity hit enters impact even when class/weapon resistance
  transforms damage to zero,
- Wand shots use selector-1 resistance behavior,
- Single- and Multi-Plasma share the selector-0/default plasma resistance
  behavior,
- the same Nitemare player->enemy difficulty factor used by Silver Pistol is
  applied after class/weapon scaling.

The projectile resistance matrix now includes the verified Wand-only Ghost
vulnerability, Wand immunity on Alien #1/#2, Baddie /256 behavior for plasma,
Vampira/Dracula/Gargoyle/robot class divisors, and the zero normal-damage
Penelope/Cannon branches. Hamerstein remains gated off until the recovered
0x7E52 state has an explicit ECWolf representation.

### Remaining fidelity boundary

The collision box, cached projected damage seed and class transform are now
represented. Original DDA/Bresenham trajectory state, native flight/impact
sequence frames, exact wall/object cell collision and 16-bit fixed-point
projection details remain future native combat work.

## Silver Pistol multi-target hitscan

Selector 2 now uses a Nitemare-specific hitscan action instead of ECWolf's
closest-target `A_GunAttack`.

The targeting path reproduces the important recovered invariants:

- one accepted shot consumes one Silver round,
- every shootable candidate is examined; the loop does not stop after the
  first accepted target,
- candidates must be admitted by the same current-map visibility rule used by
  the ECWolf sprite renderer,
- actor projection is refreshed for the shot,
- the projected sprite bounds must cross the viewport center after a 4-pixel
  horizontal expansion,
- the map/object LOS path is limited to 16 tile steps,
- normal ECWolf wall/door LOS must pass,
- intermediate `+SOLID` actor cells reject the shot, standing in for the
  original object-plane blocking test.

Generated GUARD1..GUARD26 actors now inherit from explicit runtime-class bases
`NitemareGuardClass08..NitemareGuardClass21`. This gives combat code a stable
link to the recovered OBJECT class without inferring it from a sprite name or
raw object ID. The bases use the recovered fresh GUARD strength of 255.

The verified Silver-Pistol resistance matrix is applied before HP subtraction.
Examples include:

- Baddie #1/#2: /256,
- Vampira, Dracula phases: /2,
- Gargoyles: /4,
- Trashcan robot: /16,
- Penelope and Cannon: zero normal weapon damage,
- Ghost: zero Silver damage,
- Goldie/Greenie: /2.

Hamerstein remains zero in this layer until the recovered 0x7E52 vulnerability
gate has a corresponding ECWolf level/script state; the engine does not invent
an Episode-3-only shortcut.

Nitemare MAPINFO difficulty factors now preserve the opposing original trends:

- Easy: enemy->player x0.5, player->enemy x2,
- Normal: x1 / x1,
- Hard: enemy->player x2, player->enemy x0.5.

### Remaining fidelity boundary

The candidate/LOS/resistance and projected-row seed are now represented. The
remaining Silver-Pistol fidelity gaps are GUARD state 00/09/0A exclusion,
pain/death sequence scheduling, the Hamerstein special gate, and unavoidable
fixed-point/projection differences between the original 16-bit renderer and
ECWolf.

## Eight-slot player projectile gate

Projectile-mode weapons now pass through native action
`A_NitemareFireMissile` rather than directly through ECWolf's generic missile
action.

The action preserves one important original acceptance rule exactly at the
engine level:

1. resolve the requested Nitemare projectile class,
2. count active actors derived from `NitemarePlayerProjectile`,
3. if eight are already active, reject the fire attempt,
4. only after a free slot exists, consume one unit of weapon ammo,
5. spawn the projectile.

This models the recovered single shared pool of eight 42-byte player
projectile records. Plasma and Wand projectiles, including Multi Plasma fire,
all compete for the same eight logical slots. A projectile remains counted
while its ECWolf projectile actor still exists, including its short impact
state, which corresponds more closely to original slot state 2 than releasing
the slot at first contact.

The current ECWolf projectile actor still uses bootstrap velocity, invisible
presentation and generic collision/damage. Original Bresenham/DDA fields,
embedded OBJECT animation state, +/-9 GUARD proximity collision and
projected-row damage are the next fidelity layer.

## Bootstrap player weapons

The native-data branch now exposes all four recovered Nitemare player weapons
as real ECWolf Weapon inventory items and maps OBJECT class 0x36 pickups
directly to them.

Stable OBJECT IDs and runtime selectors are:

- 0x25 / selector 0: Single bolt plasma gun,
- 0x26 / selector 1: Magic Wand,
- 0x27 / selector 2: Silver Pistol,
- 0x28 / selector 3: Multi bolt plasma gun.

Ammo routing follows the recovered executable:

- selectors 0 and 3 share `NitemarePlasmaAmmo`,
- selector 1 uses `NitemareWandAmmo`,
- selector 2 uses `NitemareSilverAmmo`,
- every accepted shot consumes one unit.

Weapon acquisition is handled by native action
`A_NitemareGiveWeapon(selector)`. It grants/owns the selected weapon, selects
it, and **initializes** its associated ammo pool to 50. This intentionally
uses assignment rather than +50, matching the class-0x36 pickup helper.

The recovered slow-counter attempt thresholds are `[2,1,3,1]` for selectors
0..3. At the nominal 8 Hz slow scheduler this corresponds to approximately
250/125/375/125 ms. ECWolf DECORATE frame durations support only whole or
half-tic values, so the bootstrap presentation uses the nearest legal
durations (9 / 4.5 / 13 / 4.5 DECORATE time units). Selectors 0, 1 and 2 are
marked non-autofire; selector 3 uses `A_ReFire` while FIRE remains held.

The firing layer is deliberately split by the recovered runtime mode:

- selectors 0, 1 and 3 currently spawn ECWolf projectile placeholders,
- selector 2 currently uses ECWolf's hitscan action.

These are enough to make weapon ownership, switching, ammo use and firing
functional. They are **not** the final combat-fidelity layer. The temporary
projectile actors use invisible bootstrap presentation, approximate movement
and damage. ECWolf's generic hitscan chooses a closest target, whereas the
original Silver Pistol can damage every eligible current-generation GUARD
whose projected sprite overlaps the center aim interval and whose 16-cell LOS
passes. The original eight-slot projectile pool, DDA movement, +/-9 GUARD hit
test and class/weapon damage matrix remain the next native combat step.

## Crystal Ball / Magic Eye charge pickups

The two automap-support resources are now represented as shared persistent
capacity-aware pools:

- OBJECT class 0x3A / raw 0x1A Crystal Ball -> +20 to
  `NitemareCrystalCharge`, max 100,
- OBJECT class 0x3B / raw 0x19 Magic Eye -> +20 to
  `NitemareMagicEyeCharge`, max 100.

The recovered Win16 player block stores these resources at 0x4C42 and 0x4C43.
Both are saved with the player state, decremented only while their associated
automap feature is active, and are not part of the normal level-setup
key/card reset. The ECWolf pools therefore use `inventory.interhubamount 1`
so their amounts survive ordinary level transitions.

The same persistence correction is applied to the three Nitemare ammunition
pools. Colored keys and ID cards intentionally remain level-local.

This commit implements pickup/storage/capacity semantics only. The Crystal
Ball/Magic Eye activation flags, slow-tick consumption, guard-marker cadence,
low-power parity gating and map-noise rendering remain a separate automap
runtime layer.

## Basic health and ammunition pickups

The generated OBJECT layer now promotes the two high-volume pickup families
needed by ordinary map traversal from structural placeholders to native ECWolf
inventory actors.

### FOOD / class 0x33

Raw IDs are stable across the three supplied episode catalogs:

- 0x12 Red potion -> +20 health,
- 0x13 Blue potion -> +10 health.

The original rule is `20 >> subtype` with a strict `HP < 100` acceptance
gate. Generated actors derive from ECWolf `Health`, use max amount 100, and
therefore leave the pickup in the world when the player is already at 100 HP.

### AMMO / class 0x39

The three stable raw IDs are mapped to shared logical ammo pools:

- 0x29 Silver bullets -> `NitemareSilverAmmo`,
- 0x2A Plasma power cell -> `NitemarePlasmaAmmo`,
- 0x2B Spell book / wand power -> `NitemareWandAmmo`.

Each pickup adds 20 and each logical pool has maximum 100. Episode-specific
sprite actors derive from these common base ammo classes, so collecting the
same ammunition in another episode does not create a second logical pool.

ECWolf's native Health/Ammo rejection behavior also preserves the original
important world-state boundary: a pickup rejected at capacity stays in the
map. The original Win16 code briefly permits values such as 99+20 before a
later HUD/state clamp; the current ECWolf implementation clamps during the
pickup operation, so final gameplay state matches but that transient internal
ordering is not claimed byte-identical.

Weapon ownership, starting-ammo grants, Crystal Ball/Magic Eye charge,
score-bearing health items and other pickup classes remain separate follow-up
work.

## Transportation Chamber doors

The Transportation Chamber wall families are now handled as the ID-card
controlled sliding doors used by the original USE dispatcher, not as level
exits themselves.

Recovered runtime classes 0x39/0x3A correspond to `DOORVI` / `DOORHI`.
Their class-relative variant selects the ID-card bit:

- Chamber Door 1 -> red ID card / Nitemare lock 205,
- Chamber Door 2 -> yellow ID card / Nitemare lock 206.

On successful credential check the original path enters the same ordinary door
activation state machine. The actual level transition remains a separate
`LEVEL_UP` wall behind or beyond the chamber door.

The generated translator therefore gives `DOORVI` vertical slide geometry
and `DOORHI` horizontal slide geometry, applies the matching ID-card lock,
and uses the existing `Door_Open` trigger path.

This change also fixes the generated ECWolf trigger for every horizontal
Nitemare sliding-door family: horizontal doors now pass `arg4 = 1` to
`Door_Open` so EVDoor moves on the horizontal axis. The visual
`offsethorizontal` flag alone was not enough to select the thinker direction.

## Remote control panels and remote doors

Episode 2's remote-control path is now wired from the original WALLS catalog
instead of being hard-coded to one map.

The relevant catalog relationships are:

- `CONTROL` #1 / #2,
- `DOORVR` #1 / #2,
- `DOORHR` #1 / #2.

The generated XLAT extracts the numeric group from the original descriptions.
CONTROL #1 requires the red ID card (lock 205) and targets remote-door group 0;
CONTROL #2 requires the yellow ID card (lock 206) and targets group 1. The
vertical/horizontal remote-door raw IDs are looked up from the same WALLS table
and passed to the runtime special, so the implementation does not bake AC/AD/
AE/AF into engine code.

`Nitemare_RemoteControl` (special 19) exposes the recovered five-command
modal menu:

- Open remote doors (original command 0x1E),
- Close remote doors (0x1F),
- Enable remote cannons (0x20),
- Disable remote cannons (0x21),
- Cancel.

The original menu-state rules are preserved: Open and Close are mutually
exclusive according to the per-group remote state; Enable and Disable are
mutually exclusive according to the global cannon-enable state.

Remote-door command state is stored as hidden, level-local Inventory markers.
This deliberately mirrors the lifecycle of the original saved level flags:
ECWolf savegames preserve the markers, while ordinary map transitions remove
them because their inter-hub amount is zero. The cannon state uses an inverted
`NitemareRemoteCannonsDisabled` marker so the normal level-start default is
enabled without a special initialization pass.

Remote doors use the existing EVDoor geometry with a Nitemare-only latched-open
mode. Open keeps the door open until a matching remote Close command. Close
uses a forced state transition without the ordinary occupancy precheck, matching
the recovered Nitemare remote-command path; actor movement/collision handles
any later overlap rather than adding door-crush damage.

The cannon menu state is now preserved and ready for class-0x19 Cannon AI.
Generated Cannon actors are still structural placeholders, so Enable/Disable
does not yet alter attacks until that AI runtime is implemented.

## Mirror / Other Side portal (WARP_S1 / WARP_S2)

The special mirror family is now connected to the recovered pentagram progress
logic.

The executable path for mapped wall type 0x15 (`WARP_S1`) checks the four-bit
pentagram mask in 0x4C45. The bits are:

- bit 0: Red Pentagram,
- bit 1: Green Pentagram,
- bit 2: Blue Pentagram,
- bit 3: Yellow Pentagram.

OBJECT class 0x3C (raw IDs 0x1F..0x22) now generates real ECWolf Inventory
pickups instead of non-interactive placeholder actors. Unlike colored keys and
ID cards, the original 0x4C45 pentagram mask has no normal level-setup reset
writer in the checked Win16 path; its writers are the pickup dispatcher and
the Omnifarious cheat. Generated pentagrams therefore use
`inventory.interhubamount 1` so progress survives ordinary map transitions.

On USE:

- `WARP_S1` with missing pentagrams shows the portal notice and the exact
  missing color names,
- with all four pentagrams it resolves the lowest raw ID belonging to
  `WARP_S2`, computes `targetRaw - currentRaw`, and reuses the recovered
  N/E/S/W free-neighbor relocation helper,
- `WARP_S2` does not teleport back; it shows the original
  `The mirror crack'd / from side to side!` notice.

This follows Win16 `FUN_1010_C126`. Its success audio/event path is not yet
bound to a named SND.DAT sound, so the portal transition currently omits that
native event.

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
