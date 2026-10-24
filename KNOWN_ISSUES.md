# Known issues and testing limits

- **Loadouts are experimental and not fully functional.** Saving, loading and
  restocking may not behave as expected. Verify pouch contents manually and
  keep backups. They are not a dependable automatic preparation workflow.
- Cuga is a development project. Not every quest, monster, area, GPU or platform
  has been checked. Multiplayer features are not a guarantee of compatibility
  with every client or relay setup.
- Native QOL offsets target the tested ULJM05500/FUComplete layout. Unsupported
  layouts may fall back to original behavior.
- Key quest labels may remain cached until restart after changing the setting.
- The capture skull is drawn by native UI. Window captures include it, but the
  game-frame-only screenshot export does not capture native UI overlays.
- Cloud providers may cache writes. Local read-back does not prove that a save
  has reached another device. Save, exit and wait for synchronization before
  switching devices; keep recovery backups.
- Windows and Android builds have been exercised during development; no broad
  cross-platform certification or complete Android regression run accompanies
  this source snapshot.
- Some inherited build and release scripts describe upstream MHP3rd behavior.
  Building a playable Cuga installation still requires developer setup and
  generation from the player's own game files.

No saves, game dumps, personal configuration, signing keys or private cloud
data should be posted in issue reports.
