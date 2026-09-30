# Source-based game backends

This branch is separate from the Nitemare 3D work. Its purpose is to add
additional games to ECWolf as independent game families, using their released
source code as the behavioral and file-format reference.

## Upstream projects

### Blake Stone

Primary references:

- https://github.com/bibendovsky/bstone
- https://github.com/johndrinkwater/blake-stone

ECWolf already contains a partial Blake backend:

- `src/g_blake/`
- `wadsrc/static/actors/blake/`
- `wadsrc/static/bs6map.txt`
- `wadsrc/static/vsimap.txt`
- `wadsrc/static/mapinfo/planet.txt`
- `wadsrc/static/xlat/planet.txt`

Planet Strike is currently registered as a Preview game. The main work here is
therefore completion, verification against the released source, and adding the
missing Aliens of Gold game definitions rather than creating a new engine
family from scratch.

Licensing note: BStone contains GPLv2 original/modified-original code and MIT
new code. The older Planet Strike source release repository does not present a
clear standalone open-source license in its README, so it is used only as a
historical/behavioral reference unless a licensed equivalent is available in
BStone.

## Catacomb family

Primary upstream organization:

- https://github.com/CatacombGames

Supported source families planned:

| Game | Data extension |
| --- | --- |
| Catacomb 3-D | C3D |
| Catacomb Abyss | ABS |
| Catacomb Armageddon | ARM |
| Catacomb Apocalypse | APC |

The released source identifies the classic ID cache file families:

- `EGAGRAPH.<ext>`
- `EGAHEAD.<ext>`
- `EGADICT.<ext>`
- `GAMEMAPS.<ext>` or `MAPTEMP.<ext>`
- `MAPHEAD.<ext>` when the map header is external
- `AUDIO.<ext>` / `AUDIOT.<ext>`
- `AUDIOHED.<ext>` when the audio header is external

Current branch implementation:

- ECWolf's Huffman graphics resource reader now recognizes
  `EGAGRAPH/EGAHEAD/EGADICT` in addition to the existing VGA family.

Still required for a playable backend:

- EGA planar image decoding and Catacomb palette translation;
- fallback for source-linked map/audio headers used by original releases;
- Catacomb map translators and tile attributes;
- actors, weapons, projectiles, keys, gates and game-state rules;
- per-game MAPINFO/IWADINFO definitions.

The Catacomb source trees are GPLv2. Any directly adapted gameplay code must
remain isolated from the BSD ECWolf core and built under the GPL configuration.
File-format adapters written independently from documented format behavior may
remain under the ECWolf core license.

## Hovertank 3-D

Primary upstream:

- https://github.com/FlatRockSoft/Hovertank3D

Original data extension:

- `.HOV`

The source uses:

- `EGAGRAPH.HOV` / `EGAHEAD.HOV`;
- `SOUNDS.HOV`;
- individual `LEVEL00.HOV` ... `LEVEL19.HOV` files.

The original level loader documents:

- 0xFEFE word-RLE;
- a 32-byte expanded level header area;
- width/height and plane count in the expanded header;
- plane 0 starting at offset 32;
- plane 1 starting at offset `32 + planesize`.

Current branch implementation:

- `file_hoverlevel.cpp` recognizes and expands `LEVELxx.HOV`;
- the first two Hovertank planes are exposed to ECWolf as an in-memory
  `WDC3.1/PLANES` map, with a zeroed third plane;
- map markers use `HOV00` ... `HOV19`.

Still required for a playable backend:

- Hovertank EGA graphics/sprite conversion;
- `SOUNDS.HOV` support;
- plane-1 spawn/object translation from HOVACTS/HOVLOOP;
- vehicle movement, charged cannon and afterburner behavior;
- enemies, refugees, shields, warp objects and mission rules;
- MAPINFO/IWADINFO and UI definitions.

Hovertank source is GPLv2. Direct gameplay ports therefore belong in a GPL-only
game module.

## License boundary

The project should keep two implementation styles distinct:

1. **Core format adapters**: independently implemented from documented binary
   formats and observable behavior. These can live beside ECWolf's existing
   BSD resource readers.
2. **Direct source-derived gameplay ports**: code adapted from GPL upstream
   source. These must remain clearly marked and compiled only in the GPL build.

Original commercial game data is never added to this repository.
