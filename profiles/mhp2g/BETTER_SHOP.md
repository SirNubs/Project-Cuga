# Better Shop

QOL > Better Shop (`game.better_shop=1`, default off) gives the village
peddler outside the guild hall the union of her five stock lists, plus
Mega Potions, Flash Bombs and Sonic Bombs. The game's own item prices,
purchase logic and carrying limits apply. Close the shop before toggling;
the new list takes effect the next time it opens.

The edit only applies to the identified ULJM05500 village overlay. It is
reversible in memory and does not patch the disc, executable or save.
Switching it off restores all original stock bytes and pointers. Other
shops and quest inventories are unchanged. Overlay reloads are handled.

Verification: synthetic-memory tests cover stock merging, additions,
restoration, menu deferral, reload and unsupported/modified data rejection.
See the local lab journal for game verification and remaining gaps.
