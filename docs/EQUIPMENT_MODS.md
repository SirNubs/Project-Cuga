# Equipment mods: which file a piece of equipment uses

An equipment mod (`Type="EquipSET"`, `EquipHEAD`, `EquipGS`, ...) is a model
without a fixed target: the player says which file of `DATA.BIN` it replaces,
and the pieces that use that file then look like the mod. The Mods page can
fill the targets in from the running game:

- **Use my current armor** points each slot at the model file of the armor the
  loaded hunter wears in that part, for the hunter's sex. A weapon mod's button
  reads *Use my current weapon* and takes the carried weapon, if it is of the
  mod's class.
- **No armor** points each armor slot at the file the game shows when that part
  is empty, for the hunter's sex and inner wear.
- Each slot shows, next to its file id, what the game draws from that file,
  with the game's own names read from the running game ("*name*, female",
  "*name* and 1 more, male", "No armor (inner wear), female").
  Typing an id stays possible.

Both buttons only read the game's memory and write only the mod's choices in
`mods.ini`, so they work in release builds and during ad hoc play. They
refuse, and say why, while no character is loaded (the title screen and the
character select) or when a weapon mod's class is not the one the hunter
carries. Felyne gear (`EquipCATSET`, `EquipCATHELM`, ...) has no buttons.

## When the hunter shows the mod

The game loads the armor models when it loads the character and when the
equipment changes at the item box; it keeps them otherwise. Measured with
`MHP3RD_TRACE_DATA_BIN=1`:

| What the player does | The armor models are read again |
| --- | --- |
| Starts the game and enters the village | Yes |
| Changes to another armor set at the item box (Manage Equipment) | Yes, at once |
| Chooses the set the hunter already wears | No |
| Walks from the village into the Guild Hall | No |

So after pressing a button: at the item box change to other armor and back,
or restart. When a mod's file is larger than the file it replaces, the Mods
page says that a restart is needed and offers *Restart now*, as for any other
mod.

Not measured: whether starting a quest or returning from one reloads them.

## What the game keeps where

All addresses are for NPJB-40001. They were found by tracing, not taken from
elsewhere: the model files the game read were logged with
`MHP3RD_TRACE_DATA_BIN=1`, memory was dumped with the developer tools'
`dump` command ([DEBUG_MENU.md](DEBUG_MENU.md)) and searched offline, and the
code that uses each table was read in the executable to confirm what it
does. The code is `profiles/mhp3rd/host/game/equipment_models.{hpp,cpp}`, and
`tests/equipment_models_tests.cpp` checks it on made-up tables.

### The character

The game reaches the loaded character through the pointer at `0x08AB3640`,
plus `0x85C`: `0x09F4FCAC`, the block that starts with the hunter's name
(`kCharacter` in `host/game/game_data.hpp`). The same layout sits at
`0x09554FA0` while the character select shows the save; the game copies it
when it enters the village.

| Offset | What | How it was found |
| --- | --- | --- |
| `+0x1B` (u8) | Sex: 0 male, 1 female | The code that builds the player copies it to the player object (`+0x458`), which picks the male or female model and file base. 1 on the save's female hunter; set to 0 in the character select's copy, the village loaded the male files for the same armor |
| `+0x1D` (u8) | Inner wear, 0 to 3 | Copied to the player object (`+0x4F0`) and used only to pick the model of an empty chest, arms or legs slot. 2 on the save's hunter; set to 0, the empty slots loaded the models of inner wear 0 |
| `+0x1E`, `+0x1F` | Face, hair | Copied beside it; the face and hair files follow from them (not used here) |
| `+0x38` (24 bytes) | The carried weapon: u8 used, u8 kind, u16 id, then weapon data | Its kind and id matched the Equipment screen's weapon |
| `+0x50` (5 × 12 bytes) | The worn armor: chest, arms, waist, legs, head, each laid out like an equipment box record (u8 used, u8 kind, u16 id, ...) | The ids matched the worn set's names in the name tables; the game's own checks read the ids at `+0x52`, `+0x5E`, `+0x6A`, `+0x76`, `+0x82` |

### Armor data

One table per part in the executable, 40 bytes per armor id; the first two u16
are the model number for a male and for a female hunter, the byte at `+4` says
who can wear the piece (bit 0 male, bit 1 female, bit 2 blademaster, bit 3
gunner).

| Part (kind) | Table |
| --- | --- |
| Chest (0) | `0x08985A7C` |
| Arms (1) | `0x08983934` |
| Waist (2) | `0x0898A6E4` |
| Legs (3) | `0x0898C854` |
| Head (4) | `0x08987EE4` |

Found by searching a memory dump for a table whose entries, by armor id, grow
with the file ids that a community file list gives for the same armor names
(the list was used only as a hint, and nothing of it is in the repository);
the only stride that fit was 40 bytes, and the five tables lie one after
another. The executable's code then showed the game reading them by kind with
exactly these bases, and the male or female half by the sex byte.

### From a model number to a file

The file is a base per sex and part plus the model number, and the bases are
a table in the executable at `0x089E892C`: u16 `[sex][part]`, seven parts per
sex (chest, arms, waist, legs, head, hair, face). On NPJB-40001 the armor parts
have 125 models each. The game's code adds the table's base to the model
number it got from the armor table; the result is the file it reads.

A model number of 0 is the bare part, but for chest, arms and legs the game
shows the inner wear instead: a pointer per sex at `0x089E8704` (chest),
`0x089E86EC` (arms) and `0x089E871C` (legs) leads to a list of model numbers by
inner wear (60 to 63 on this disc). Waist and head show model 0.

Checked in the running game with the 100% test save (a female hunter wearing
the Silver Sol set, inner wear 2):

| Case | Predicted and read |
| --- | --- |
| Female, Silver Sol set | head `02BB`, arms `0144`, chest `00C7`, waist `01C1`, legs `023E` |
| Female, empty chest | chest `00C3` (inner wear 2 of the chest); the Gordon mod's chest aimed there showed on the bare hunter |
| Female, everything empty | `0279`, `0140`, `00C3`, `017F`, `023A` |
| The same hunter made male, Silver Sol, empty chest | chest `034B`, arms `03CE`, waist `044B`, legs `04C8`, head `0545` |
| Male, Hunter set (changed at the item box) | `0317`, `0394`, `0411`, `048E`, `050B` |

That also explains the ids a male hunter with no armor reads (head `0503`,
arms `03C8`, chest `034B`, waist `0409`, legs `04C2`): inner wear 0 of the
chest, arms and legs, and model 0 of the head and waist, which is what the
community lists call "no equipment" for every part.

### Weapons

A weapon's record starts with its model number too. Its file is a base per
weapon class plus that number, kept below the class's model count: bases u16
at `0x089CD22C` and counts u16 every 4 bytes at `0x089CD1F8`, by class, which
is the kind minus 5 (kind 10 is unused and has no models). The record tables
by kind, as the game's code picks them:

| Kind | Table | Record |
| --- | --- | --- |
| 5 Great Sword | `0x08997AA0` | 28 |
| 6 Sword and Shield | `0x089953B0` | 28 |
| 7 Hammer | `0x08994054` | 28 |
| 8 Lance | `0x0899669C` | 28 |
| 9 Heavy Bowgun | `0x08990464` | 80 |
| 11 Light Bowgun | `0x08991954` | 80 |
| 12 Long Sword | `0x08997138` | 28 |
| 13 Switch Axe | `0x08992F0C` | 28 |
| 14 Gunlance | `0x08995E14` | 28 |
| 15 Bow | `0x0898EB14` | 80 |
| 16 Dual Blades | `0x08994A9C` | 28 |
| 17 Hunting Horn | `0x089936EC` | 28 |

Checked by the files read for two great swords (`05F4`, `05DD`) and a long
sword (`06B7`). The other classes follow the same code and were not loaded;
bowguns and bows may use more files than one.

## Not verified

- Weapon mods: the formula was checked against the files the game read, and
  the button against a stand-in mod (it set the great sword's file, and
  refused a long sword mod while the hunter carried a great sword), but no
  real weapon mod was shown on the hunter.
- A male character: the save has only a female hunter, so the male case was
  made by changing the sex byte of the character select's copy in memory (never
  saved). A hunter created male was not tried.
- Armor pieces that share a model share a file: a mod aimed at one shows on
  all of them. The slot's description says "and N more" when that is so.
