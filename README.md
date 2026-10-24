# Project Cuga

An experimental native port of **Monster Hunter Portable 2nd G / FUComplete**,
with Windows and Android development builds and quality-of-life additions.

**Built on [Project Poke by francl35](https://github.com/francl35/Poke), which
is built on [Yakumo by TeamGDB](https://github.com/TeamGDB/Yakumo).** The original
MHP2G port, PSP runtime, recompiler, renderer and substantial host implementation
come from those projects and their contributors. Cuga is an independent fork,
not an official release or successor of either project. Please report Cuga
issues here rather than to its upstream projects.

> **Loadouts are experimental and not fully functional.** Saving, loading or
> restocking a loadout may not behave as expected. Check your item pouch after
> using them and keep a save backup. Do not rely on loadouts for automatic item
> preparation.

## Status

This is a **development source snapshot**, not a finished release or a promise
of full game compatibility. Windows gameplay and individual features have been
tested; Android builds have also been tested during development. Broad hardware,
full-game, multiplayer and cross-device regression testing remains incomplete.
The inherited MHP3rd profile is retained for framework compatibility; its presence
does not make this a supported release of upstream Yakumo.

## Features

- Analog camera controls and controller support.
- Online hall and chat functionality inherited and extended from the port.
- **Better Shop:** the peddler by the guild hall offers her combined rotating
  stock, plus Mega Potions, Flash Bombs and Sonic Bombs.
- **Key Quest Stars:** marks key quests for the Village Elder, Nekoht and Guild
  Hall. Restart after toggling to refresh loaded quest lists.
- **Capture skull:** eligible monster markers become skulls when capturable.
  Works with paintballs and the **Monsters on the map** QOL option.
- Save import and recovery backups.
- Optional Windows/Android cloud saves using a shared file you select through
  your cloud provider. Configure it yourself; play on one device at a time and
  let the provider finish syncing before switching.

See [Better Shop](profiles/mhp2g/BETTER_SHOP.md),
[cloud saves](profiles/mhp2g/CLOUD_SAVES.md), and
[known issues](KNOWN_ISSUES.md). Loadouts remain incomplete as noted above.

## Bring your own game files

This repository contains **port source code and documentation only**. It does
not contain a game dump, save, extracted game assets, generated game-code
corpus, compiled overlays, personal settings or cloud-save files.

You must provide files from your own legally obtained copy of MHP2G, and apply
the appropriate FUComplete patch yourself if using that setup. The native QOL
features target the verified ULJM05500/FUComplete layout; other layouts may
disable features or require additional work. A US/EU save import does not mean
the US/EU executable is supported.

**Download ZIP gives you source, not a ready-to-play game.** Local game-derived
code and overlay libraries must be generated during a developer build. A
separate binary release is not included in this initial source upload.

## Building and running

The MHP2G CMake target is named `Poke`; it produces `Cuga` / `Cuga.exe`.
Start with [the MHP2G build notes](docs/CUGA_BUILDING.md) and the
[general build guide](docs/BUILDING.md). Some inherited guides describe MHP3rd
and upstream release tooling; those instructions are not all verified for Cuga.

The checked-in source can build synthetic tests without game data. A playable
build needs locally generated main code and compatible overlay libraries.
Keep those outputs, your game directory and saves outside version control.

On Windows, open QOL for optional gameplay conveniences and System for save
import, cloud-save configuration and recovery. Disabling QOL options restores
normal display behavior; the title markers require a restart to refresh.
Back up saves before installing an experimental build. Keep your data folder
when replacing or removing the executable if you want to retain progress.

For troubleshooting, provide your platform, build, steps to reproduce and a
redacted error log. Never attach game dumps, saves, signing keys or cloud data.

## Credits and license

- **[Project Poke / francl35](https://github.com/francl35/Poke):** original MHP2G
  native-port groundwork and profile.
- **[Yakumo / TeamGDB](https://github.com/TeamGDB/Yakumo):** PSP runtime,
  recompilation framework, renderer, tools and upstream implementation.
- **PSPRecomp contributors and all upstream contributors:** retained copyright
  and license notices.
- Third-party libraries retain their own licenses. See
  [source provenance](docs/SOURCE_PROVENANCE.md) and the bundled notices.
- Cuga development used AI coding assistance, including **Codex / GPT-6** and
  earlier **Claude Code** sessions, with human direction and playtesting.

Port source follows the inherited [MIT License](LICENSE). That license does
not grant rights to Capcom or Sony game content. Project Cuga is unofficial
and is not affiliated with or endorsed by Capcom, Sony, Project Poke or Yakumo.
