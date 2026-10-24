# Layered armor: how the hunter's look is kept apart from its armor

Layered armor draws the hunter played on this machine in other armor pieces
than it wears, part by part, while the game keeps the real pieces for
everything else. The player's side is in the profile README
([Layered armor](../profiles/mhp3rd/README.md#layered-armor)); this page says
what the game does, how that was found, and why the rest of the game cannot
see the difference.

Everything here is for NPJB-40001. It was found by reading the executable's
code around the tables that [EQUIPMENT_MODS.md](EQUIPMENT_MODS.md) traced
(the armor records and the file bases) and by tracing the running game:
temporary wrappers around the functions below logged their arguments, return
values and callers (`MHP3RD_TRACE_LAYERED_ARMOR=1` keeps the part of that the
feature needs), memory was read with the developer tools' `peek` and `find32`
([DEBUG_MENU.md](DEBUG_MENU.md)), and every step was checked in the game with
the 100% test save (a female hunter in the Silver Sol set).

The code is `profiles/mhp3rd/host/game/layered_armor.{hpp,cpp}` (pure
functions over a `Ram`, tested on made-up tables in
`tests/equipment_models_tests.cpp`) and `layered_armor_hook.cpp` (what is put
in the game's place), and the page is `host/ui/layered_armor_screen.cpp`.

## Where the game decides what a hunter looks like

### The hunter records

The block the game reaches through the pointer at `0x08AB3640` holds, from
`+0x30`, a 252-byte record per hunter. Record 0 is the hunter played here;
records 1 to 3 are the other hunters of an ad hoc session (0x08872DC8 fills
them from per-player data, from record 1 on, 252 bytes apart). The block's `+0x85C` is the character that
[EQUIPMENT_MODS.md](EQUIPMENT_MODS.md) describes.

| Offset in a record | What |
| --- | --- |
| `+0x00` (u8) | In use |
| `+0x08` (u32) | Bit 0: female; the next bits hold the hair and face (read with `ext` for parts 5 and 6) |
| `+0x1C`, `+0x26`, `+0x30`, `+0x3A`, `+0x44` (u16) | The armor ids by part: chest, arms, waist, legs, head, 10 bytes apart |

Record 0 held exactly the worn Silver Sol ids (chest `0xAB`, arms `0xA5`,
waist `0xA5`, legs `0xA6`, head `0xA9`), the same as the character block.

### The model lookup, 0x08869778

`0x08869778` takes the block (a0), a record index (a1) and a part (a2, 0 to
6) and returns a model number: for an armor part, the male or female half
(by the record's bit 0) of the armor table's entry at the record's id. It
only reads memory. Everything that draws a hunter asks it:

| Caller | What it does with the model |
| --- | --- |
| `0x088A58DC`, a hunter's file function (vtable `+0xA8`) | File = base per sex and part (`0x089E892C`) + model, after `0x088A51E4` turns model 0 of the chest, arms or legs into the inner wear: the file the hunter loads for that part |
| `0x088A5B20` | Keeps the seven models at the hunter's `+0x4F6`, which `0x088A5DC0` reads to choose the part's colours |
| `0x088B9AD0` | Special cases of some head models (112, 114, 118) |
| `0x08891214` | The hair (part 5) |
| `0x08869C74` | Whether the head is model 112; in the same generated unit, so a direct jump (see below) |

A hunter object keeps its record index at `+0x60` (`0x088BA4C0` sets it when
it makes the hunter for a record), and all of the above read that index. In
the trace, every armor model that the village, the Guild Hall and a quest
loaded for the hunter played here went through `0x08869778` with index 0.

Much other code reads the armor tables by id (about a hundred places in the
executable): the Equipment and Status screens, defense, resistances and
skills, the smithy, the item box. None of it goes through `0x08869778`, and in
the game the Equipment screen on a quest listed the real Silver Sol pieces
with their skills while the hunter was drawn in the Yukumo set.
`0x08869564`, a second model lookup by id rather than by record, serves
another kind of hunter object (`0x088F46D8` is its file function; one such
object was seen while the village loads, not what it is for); layered armor
leaves it alone.

### What layered armor changes

While it is on, `0x08869778` is replaced (the runtime's function table, at the
flip, as the analog camera and the HUD toggle do). For index 0 and a part
with a chosen piece it returns that piece's model for the record's sex,
exactly what the game's function returns for that piece; anything else runs
the game's own code. The file, the inner wear of *Nothing*, the colours and
the head's special cases then follow as for a piece really worn.

It writes nothing: not the records, not the character, not the equipment
box. Turned off, the game's own function goes back in the table. Off from the
start, nothing is registered at all. A different executable is left alone:
the replacement is only installed when the code at the traced addresses is
the code described here.

The one caller it does not reach is `0x08869C74`, which the generated code
reaches with a direct jump inside its unit, not through the table: that check
(is the head model 112?) keeps seeing the real head. It is called from
`0x088CAEC8`; what that serves was not traced, and nothing was seen to change
because of it.

## When a new look shows

A hunter loads its part models with a state machine at `0x088A5564`
(vtable `+0x94`), which `0x088BD1CC` steps once a frame while the game loads
something, until it reports done. `+0xB80` is the state (0 start, then one
step per part, 3 and above done), `+0xB84` the part it is at, and `+0xB8C` the
file loaded for each part; each step asks the file function for the part and
loads the file only when it differs from the one loaded. The game's own
request to load them again, `0x088A53C8`, sets the state and the part to 0.
The game steps it when the hunter is made (entering an area, starting or
leaving a quest) and after a change of equipment at the item box.

The model lookup alone changes nothing on screen until the hunter loads its
models again. So layered armor also wraps the step: when the game steps the
hunter played here (index 0, drawn from its record) with its load done, and
the look differs from the one it was last loaded with, it sets the state and
the part to 0 as the game's request does, and the game's step goes on from
there. A change thus shows at the next load the game makes by itself, and
only the parts whose file changed are read. Nothing is ever started outside
the game's own loads: setting the state while the game was not loading
anything was tried, and the loader then never ran, since the game steps it
only while it loads.

Measured with `MHP3RD_TRACE_LAYERED_ARMOR=1`: a change made in the Guild Hall
showed on leaving it; a change made just before departing showed on the
quest; turning the feature off in the village showed the real armor on
entering the Guild Hall.

## Multiplayer: what other players see

Other players see the real armor. Each game draws the other hunters from its
own records 1 to 3, which it fills from the data the other games send
(`0x08872DC8`); what this game sends is its own equipment, which layered
armor never changes, and the replacement applies to record 0 only, so the
other hunters are drawn here as their games describe them. The look is never
sent, and a session shows nothing new to the others.

Not verified in a session: the packet that carries the equipment was not
traced on the wire, and no two-instance session was run with layered armor
on. The claim rests on the replacement writing no memory at all.

## Not verified

- A hunter created male (the save has only a female hunter). The male half
  of the table is chosen by the same bit as the game's own code.
- What the objects drawn through `0x088F46D8` are; they keep the real pieces.
- Changing equipment at the item box with layered armor on.
- Pieces with special heads (head models 112, 114, 118) chosen as a look.
- An ad hoc session with layered armor on.
