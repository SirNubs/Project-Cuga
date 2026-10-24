# Developer tools: the Debug page

A page of cheats in the in-game menu, for testing without grinding: money,
any item or equipment piece put straight into the boxes, any village or
Guild Hall quest started straight from the village or the Hall, and on a quest infinite health and
stamina, a frozen clock and monsters at 1 health. The same tools can be
driven from a script through a command file.

**It is for developers only.** It is not in release builds, and it never
writes anything while ad hoc play is on, so a test cannot reach another
player's game.

## Turning it on

| Build | What you get |
| --- | --- |
| Release (`-DMHP3RD_RELEASE=ON`, the builds on the releases page) | Nothing. `host/debug/` and the page are not compiled, whatever else is set |
| Developer build (the default, `MHP3RD_RELEASE=OFF`) | Compiled in (CMake option `MHP3RD_DEBUG_MENU`, on by default; `-DMHP3RD_DEBUG_MENU=OFF` leaves it out), but hidden |
| Developer build run with `MHP3RD_DEBUG_MENU=1` | The menu (Esc, or L3+R3) has a **Debug** tab after System |

It is opt-in at run time on purpose: people who build from source to play
never see it by accident.

While the game has ad hoc networking on, or is in a session, joining one or
hosting, every write is refused and logged as refused; the page shows why.
Reading (the status lines) still works.

Every change is logged as a `[debug] ...` line on the console and in the log,
for example `[debug] gave 99 of 99 <item name> (#9); box holds 112`, and the
page shows the last few.

## What the page does

- **Money and points**: shows zenny and both point balances; adds 100,000
  zenny, sets zenny to 9,999,999 (the most the game shows), adds 100,000 of
  each point balance.
- **Item box**: *Give or remove items* opens a list of every item the game
  has, with the name the game itself shows (read from the running game, so a
  translation mod's names appear as the game shows them). Search by part of a
  name, filter by group (materials, consumables, ammo, decorations, other),
  choose an amount (1 to 990; stacks hold 99), or switch to *Remove* to take
  every stack of an item out. *Fill materials* puts 99 of every material the
  box does not hold yet into the free slots.
- **Equipment box**: *Give equipment* lists every piece of one kind (the five
  armor parts and the twelve weapon classes), layered and collaboration sets
  included, and puts the chosen piece, new and at level 1, into the first free
  slot. Equip it from the item box in the hunter's house as usual.
- **Start a quest**: *Start a quest* lists every village quest (one to six
  stars) and every Guild Hall quest (one to eight stars) the game has, in
  sections by board and stars, with the names, main monsters and fees the
  game's own quest lists give (so a translation mod's names appear as the
  game shows them). Search by part of a name, a monster, the objective or an
  id; choosing a quest closes the menu and the hunter leaves for it as if it
  had been accepted at its counter (the Yukumo Chief's, or the Hall's) and
  left by its gate: the counter's fee is paid, and the quest's map,
  monsters, clock and rewards are the game's own. Either kind starts from
  the village or from the Hall; after the quest the hunter comes back where
  the quest belongs: to the village after a village quest, to the Hall after
  a Hall quest. It works while the hunter walks around the village or the
  offline Hall, with no menu or dialog of the game open; in the house, on
  the farm, at the hot spring, on a quest, during ad hoc play or with a menu
  open it is refused and the page says why. The tool does not check whether
  the save has unlocked the quest or whether the hunter's rank allows it.
  Event and downloaded quests are not listed.
- **On a quest**: *Infinite health*, *Infinite stamina*, *Freeze the quest
  timer* and *Monsters at 1 health* are held: applied at every flip while
  they are on and a quest is running. The page also shows the time left, the
  hunter's health and each large monster's health.

Save in the game as usual to keep what was given. The changes are in the
game's own memory, so they are saved exactly like anything the game did
itself.

## How it works

`host/debug/` holds everything; the page is `host/ui/debug_screen.cpp`.

- `game_state.{hpp,cpp}`: the addresses and layouts only the tools use, and
  the functions that read and change them. What the rest of the host reads
  too (the character, the game's text, the equipment kinds and their name
  tables) is in `host/game/game_data.{hpp,cpp}`, which release builds compile
  as well: the Mods page reads the worn armor from it
  ([EQUIPMENT_MODS.md](EQUIPMENT_MODS.md)). Both work on a small `Ram`
  interface (`host/game/guest_ram.hpp`), so `tests/debug_tools_tests.cpp`
  runs them on a buffer.
- `debug_tools.{hpp,cpp}`: the switch, the ad hoc guard, the log, the held
  cheats and the request queue. The page never writes guest memory itself: it
  queues a request, and requests run between two game frames, on the thread
  that runs the game (`debug::frame` is called from the flip in
  `hle_media.cpp`, before the menu is drawn). The menu itself also runs at the
  flip, so a request made from it runs at once, still between frames, even
  while the menu pauses the game.
- `debug_console.{hpp,cpp}`: the command file.

Nothing outside the host changed: no runtime header, no generated code, no
overlay.

## The command file

`MHP3RD_DEBUG_COMMANDS=<file>` (with `MHP3RD_DEBUG_MENU=1`) names a file read
while the game runs, like `MHP3RD_INPUT_LIVE`: each line appended to it runs
at the next flip, and its answer is printed as `[debug]` lines. Together with
`MHP3RD_INPUT_LIVE` and window captures it drives a whole test from a shell.

```bash
echo "state" >> cmd.txt              # the character, zenny, free slots
echo "give 9 20" >> cmd.txt          # 20 of item 9
echo "quest start 505" >> cmd.txt    # a 5-star village quest
echo "quest list hall 8" >> cmd.txt  # the Guild Hall's 8-star quests
```

| Command | What it does |
| --- | --- |
| `state` | Character name, zenny, free item and equipment slots, how many item names were read |
| `money N` | Zenny to N (at most 9,999,999) |
| `give ID [N]` | N (default 1) of item ID into the item box |
| `remove ID` | Every stack of item ID out of the box |
| `fillmats [N]` | N (default 99) of every material the box lacks |
| `giveequip KIND ID` | One equipment piece of that kind byte and id (kinds below) |
| `item ID` | An item's name, group, rarity, pouch limit and how many the box holds |
| `table T [FIRST] [N]` | Entries of the game's text table T |
| `quest` | Time left, the hunter's health, each large monster's health |
| `quest list [village\|hall] [STARS]` | The village and Guild Hall quests (id, board, stars, name, main monsters, fee), or those of one board or star level |
| `quest start ID` | Leave for quest ID (village or Hall) as its gate does after its counter (see *Starting a quest* below) |
| `monsterhp N` | Every large monster's health to N (at least 1) |
| `find8`/`find16`/`find32 V [LO HI]` | Every place in user memory holding V; narrowed with `next V`, `changed`, `unchanged`, `delta D`; `list` shows them |
| `findbytes HEX` | Every place holding that byte string |
| `peek ADDR [N]` | N bytes (default 64) as hex |
| `poke8`/`poke16`/`poke32 ADDR V` | Write one value |
| `dump PATH [ADDR LEN]` | Guest memory to a file (all of it by default), for comparing snapshots offline |

The writing commands are refused during ad hoc play like the page's.
`quest start` answers `started quest ...` or `not started, <why>: ...`; a
script waits for the quest's `[overlay] installed game_task` line (or polls
`quest`) before it goes on.

## What the game keeps where

All addresses are for NPJB-40001, the one executable Yakumo supports, and
were measured in the running game with the tools above: a value the game
shows was searched for, changed in the game, searched again, and the survivor
written to see the game follow. A community cheat list for this disc gave
hints for money, the item box, the player's health, the quest clock and the
monster table; each was checked in this build as described, and no code from
it is used.

### Character and money

| What | Where | How it was found |
| --- | --- | --- |
| Zenny (u32) | `0x09FAC8D4` | The save's funds (shown on the character select screen) searched with `find32` in the village: one place. Changing it changed the Status screen's *Funds* |
| Yukumo Points, Guild Points (u32) | `0x09FAC8CC`, `0x09FAC8D0` | Next to the money; their values match the Status screen's |
| Hunter name (UTF-16, 12 characters) | `0x09F4FCAC` | Found beside the equipped set; fullwidth letters. Empty on the title screen, which is how the page tells whether a character is loaded |

A second copy of the money sits at `0x095B1BC8` while the save is being
loaded; the game copies the character to the addresses above when it enters
the village.

### Item box

1000 slots of `{u16 item id, u16 count}` from `0x09F52CF4`; id 0 is an empty
slot. Found from the hint's address; its contents matched the box in the
game, and an item given at an empty slot showed up in the house's item box
with the count given. Stacks never hold more than 99. Right after it, from
`0x09F53C94`, the game keeps other records, so nothing writes past slot 999.

### Item names and groups

- The game loads one block of its text at start (`0x08A40640`): a header of
  32-bit offsets to tables; each table a list of 32-bit offsets from the
  table to UTF-8 strings, ending with `0xFFFFFFFF`. Table 3 holds the item
  names by item id (979 of them, id 0 a placeholder), table 4 their
  descriptions, table 2 among others the monster names, and tables 5 to 38
  the equipment names and descriptions in pairs. Found by searching a memory
  dump for an item's name and walking back to the offset list and the header
  that points at it. The page reads names from here at run time; nothing of
  the game's text is in the repository.
- The item data, 20 bytes per item id, is part of the executable at
  `0x089D0FA0` (found by searching for the pouch limits of the first
  consumables in a row): `+4` a category (0 items, 1 ammo and coatings, 3
  decorations), `+5` rarity, `+6` how many the pouch holds, `+7` non-zero for
  items used from the pouch, `+12` buy and `+16` sell price. The page's groups
  come from these: materials are category 0 items the pouch holds 99 of and
  that are not used.

### Equipment box

1000 slots of 12 bytes from `0x09F4FE14`, ending where the item box begins:

| Offset | Size | Meaning |
| --- | --- | --- |
| `+0` | u8 | 1 for a used slot, 0 for an empty one |
| `+1` | u8 | Kind (below) |
| `+2` | u16 | Id within the kind: the index into that kind's name table |
| `+4` | u16 | Armor level; for weapons flags the game sets |
| `+6` | u16 × 3 | The item ids of the decorations in its slots |

Found by searching a dump for the ids of the equipped armor (looked up by
name in the name tables) and finding the equipped pieces' records; the list
of the equipped pieces' box indices at `0x09F54B1A` (weapon, chest, arms,
waist, legs, head) confirmed which record is which. The kind of each weapon
class was told apart by poking the equipped weapon's slot to other records
and reading the class the Equipment screen showed, and by which name table
each kind's ids fit.

| Kind | Name table | Kind | Name table |
| --- | --- | --- | --- |
| 4 head | 29 | 5 Great Sword | 5 |
| 0 chest | 31 | 6 Sword and Shield | 7 |
| 1 arms | 33 | 7 Hammer | 9 |
| 2 waist | 35 | 8 Lance | 11 |
| 3 legs | 37 | 9 Heavy Bowgun | 13 |
| | | 11 Light Bowgun | 15 |
| | | 12 Long Sword | 17 |
| | | 13 Switch Axe | 19 |
| | | 14 Gunlance | 21 |
| | | 15 Bow | 23 |
| | | 16 Dual Blades | 25 |
| | | 17 Hunting Horn | 27 |

Kind 101 records are talismans; the page does not make them. Verified by
giving one piece of every weapon kind and a full collaboration armor set,
then finding each in the equipment box under its own name and class icon, and
equipping the armor.

### On a quest

The task slot at `0x0A05E600` holds `game_task.ovl` while a quest runs and
`lobby_task.ovl` in the village and the Guild Hall (the overlay's header names
it). The addresses below hold other things outside a quest, so the tools read
and write them only while `game_task.ovl` is there.

| What | Where | How it was found |
| --- | --- | --- |
| Health now (s16) | `0x09649B16` | The hint pointed at `0x09649B58`; a dump on a quest showed 150 there and at `0x09649B16` and `0x09649B56`. Writing 50 at `0x09649B16` emptied two thirds of the health bar at once |
| Recoverable health (s16) | `0x09649B56` | Writing it alone changed nothing on screen until health moved: it is the red part of the bar |
| Most health (s16) | `0x09649B58` | 150, the Status screen's *Health* |
| Stamina now (float) | `0x09649DF0` | Three dumps: before sprinting, after sprinting, after resting; the float there went 724, 521, 900 |
| Most stamina (u16) | `0x0964A49A` | 900: the game counts 6 units per point shown |
| Quest time left (u32, frames at 30 a second) | `0x09FB4E68` | Dumps a few seconds apart: the one word that fell by 30 a second, from the 50 minutes of a fresh quest (90,000) |
| Quest time limit (u32) | `0x09FB4E64` | 90,000 beside it for a 50-minute quest |
| Monster table | `0x0A1B0AE0` | Pointers to the large monsters (and companions), in the quest overlay's data. The hint's code read it; on the quest, the first entry's object held the kind of the monster the quest listed and 4400/4400 health |
| Monster kind, health, most health | object `+0x62` (u8), `+0x246` (s16), `+0x288` (s16) | As above. The kind indexes the monster names in text table 2 from entry 308 |

### Starting a quest

How the counter and the gate hand a quest to the game was traced with the
maintainer playing a traced build (`PSPRECOMP_WATCH_WRITE` on the quest
state, `MHP3RD_TRACE_IO` for the files, memory dumps before and after each
step), then read in the code the traces pointed at.

| What | Where | How it was found |
| --- | --- | --- |
| The quest lists | DATA.BIN entries 4059 to 4066, one per star level | `MHP3RD_TRACE_IO` showed the entry the game reads when a star level is chosen at the counter; the file is the list the counter shows, found again in memory. Levels 1 to 6 hold that level's village quests (ids 101 to 699, the level in the hundreds) and the Guild Hall's (ids 1SSNN, 10101 to 10622, stars in SS); 7 and 8 only Hall quests (10701 to 10840): 92 village and 184 Hall quests in all. A list is 32-bit offsets to the records, ended by 0; a record has the fee at `+4`, the reward at `+8`, the time limit at `+0x10`, the id (u16) at `+0x1C`, the stars at `+0x1E`, and from `+0x48` the name, objective, failure conditions, description, main monsters and client. Checked against the counter's screen: *Harvest 'Shroom*, reward 300z, fee 0z, 50 minutes |
| The quest itself | One DATA.BIN entry per quest, loaded by the quest overlay by id | After departing on quest 501 the quest overlay read entry 3098, on 10503 entry 3484; the entries are encrypted and are not read by the tools |
| Accepted quest id (u16) | `0x09FAF8C2` (character pointer at `0x08AB3640`, `+0x60472`) | A write watch on it: accepting at the counter wrote the quest's id (101, 302, 501, 10503, ...), and the village writes 1 when it loads. Cancelling at the counter does not write it. Changing it after accepting and before leaving made the other quest start, with its own map and monster |
| The gate left by (u8) and a flag (u8) | `0x09FAF8C8`, `0x09FAF8C9` (`+0x60478`, `+0x60479`) | Written by the departure code: 2 after leaving by the village gate, 3 by the Hall's (traced in both); -1 after a save is loaded. When the village loads after a quest, `lobby_task.ovl` (`0x0A08F4E8`) reads the byte as an index into its table of places (`0x0A184068`, 20 bytes each) and puts the hunter there: a quest left by the Hall's gate comes back to the Hall. The flag is set only when a bit of the network state is (online play); offline it is 0 |
| Where the hunter is | The map overlay in slot `0x0A055E80` | `P_v00a00d.ovl` in the village, `P_v00a01d.ovl` in the Guild Hall (`MHP3RD_TRACE_IO` and the `[overlay] installed` lines on walking in); `P_v00a02d` to `a04d` are the other village areas |
| The next scene | Word pointed to by `0x0A25DD28` (`0x0A25DD2C`) | 0 while the hunter walks around the village or the Hall, other values while a menu or dialog is open (12 with the game's menu, 0x3E seen while the hunter stood at the Hall's gate before pressing □); 30 at departure in the dumps after pressing □ at the gate |
| The departure | Village code at `0x0A09CE6C` (in `lobby_task.ovl`), called from the □ handler at the gate (`0x0A0ECC90`), the only caller; the Hall's gate is the same handler, with gate 3 | Found from the village's teardown, which the write watch caught at departure: it runs once bit 4 of the scene flags is set, and turns into a quest when the scene word is 30. The functions that set that bit also set the scene word; this is the one that sets 30. It sets the word at `+0x28` to -1, the gate byte, the scene to 30, the flag byte (bit 0 of the word at `*(0x09FC8BE8) + 0x01500000 - 0x57DC`), and ends the village scene: bit 4 of the flags at `+0x20` of the scene object at `*(0x08ABAE14)`. The village then tears itself down and the quest overlay loads |

The tool does what the counter and the gate do, in the game's order: takes
the fee from the hunter's zenny (the counter took 150z for a 150z quest),
writes the id, then what the departure function writes. It never calls game
code. Compared with *Violent Carnival!* accepted at the counter and left by
the gate on the same save: the same health (100/100), the same fee taken,
the same clock, and the Bulldrome's health within the range the game gives
it from one start to the next (900 after the counter; 774 and 900 started
by the tool).

Tested from the village with the full test save, started by `quest start` and
from the page, each with the right map, monster, clock and *Current Quest
Details*: 204 *Blue Bear: Arzuros* (2 stars, Misty Peaks, Arzuros), 304
*Violent Carnival!* (Misty Peaks, Bulldrome), 309 *Toxic Troublemaker*
(Flooded Forest, Great Wroggi), 403 *Rockslide* (Sandy Plains, Volvidon),
505 *King of the Sky!* (Deserted Island, Rathalos), 606 *Roar of the Tundra*
(Tundra, Tigrex; also from the page, where the search found it by
"Tigrex"). Refused with the game's menu open and during a quest. The
maintainer started 204 and 505 from the page, set *Monsters at 1 health*,
hunted the Arzuros and the Rathalos with one hit each, and the quests ended
as usual: the monster's health reached 0, the clock stopped and the hunter
was back in the village, with more zenny after 204 (by 2,850z).

**Guild Hall quests from the village.** What the Hall's departure sets
beyond the id was checked in the traces and the code:

- The same departure function, with gate 3 (the byte the game wrote on the
  maintainer's Hall departures), and the same online flag, 0 offline.
- The character's quest state around the id differs between the village and
  the offline Hall only in that gate byte (dumps in both, compared).
- There is no separate Hall or party state to set offline: the Status
  screen shows *Yukumo Village (Offline)* in both; *Guild Hall* is the
  online hall, which the tools refuse.
- The quest's own data, rank included, comes from its encrypted DATA.BIN
  entry, which the quest overlay loads by id: a Hall quest's monsters are
  the Hall's. The Rathalos of *Hunt the King of the Skies!* (10510) had
  4,256 health, the village's *King of the Sky!* 2,280; the Great Wroggi of
  10614 2,750, the village's 309 1,125.
- The counter checks the hunter's rank before it offers a quest; the tool
  does not.

Started by `quest start` from the village, each with the right map,
monsters, clock and *Current Quest Details*: 10304 *Field Trip* (3 stars,
Great Jaggi, reward 2,400z), 10510 *Hunt the King of the Skies!* (5 stars,
Rathalos, 6,000z), 10614 *Purple Mist in the Forest* (6 stars, two Great
Wroggi), 10735 *Clobbering Time!* (7 stars, Volcano, Steel Uragaan then
two Great Wroggi and a Rathalos, 15,900z). From the Hall: 10801 *Pair of
Thunderous Roars* (8 stars, two Zinogre). After abandoning 10510 and 10304
the hunter came back to the Hall. The page lists both boards (searched for
"Rathalos": village and Guild Hall sections).

The maintainer then started 10510 from the village on the page, hunted the
Rathalos (4,446 health) with *Monsters at 1 health*, and the quest ended as
usual; the hunter came back to the Guild Hall, with 10,880z more. From the
Hall the maintainer started the village quest 101, and after it the hunter
came back to the village. Hunter rank points were not checked.

## Not verified, and not done

- Starting a quest: event and downloaded quests are not offered and were
  not traced. Starting from the house, the farm or the hot spring is
  refused, not tried. Quests are started whether or not the save has
  unlocked them or the hunter's rank allows them. Whether a Hall quest
  started this way awards hunter rank points as the counter's would was not
  checked.
- *Unlock all quests*, village progress flags and hunter rank are not on the
  page. The hint list has a quest-flag area, and a byte beside the points
  that looked like the rank (`0x09FAC8C5`: 6 on a rank 6 hunter, but 0 on a
  rank 1 hunter) did not change the Status screen's HR when written, so
  neither is understood well enough to write without risking an
  inconsistent save.
- The quest addresses were traced on a gathering quest in the Misty Peaks,
  and the page's switches verified on a low-rank hunting quest there: with infinite health on, health written down
  to 20 was back at its most by the next read; stamina stayed at 900 while
  the hunter sprinted; the clock stood at the same second for over ten
  seconds; with monsters at 1 health the quest's Arzuros read 1/960. Killing
  a monster with that one hit was not tried, nor were multi-monster quests,
  arena quests or the Guild Hall with companions.
- Saves: a late save (every armor, HR 6) and an early one (HR 1, three hours
  played). With the early one the page read the character, set zenny to
  9,999,999 (the Status screen showed it) and gave an item and an armor
  piece; that equipment was not opened in the game's equipment screen on the
  early save.
