# Cloud saves

Connect Windows and Android to the same `Project-Cuga.cuga-save` file. On
Windows choose a folder synced by the cloud provider. On Android select
the actual shared document through the system picker; Google Drive file
selection uses persistent read/write permission, not a password in Cuga.

In System, scroll to Cloud saves and choose **Connect cloud save...**.
Automatic cloud saves starts disabled until configured. Restart after
connecting or changing the setting.

At startup Cuga compares hashes of the local save, cloud save, and last
successful sync. It downloads cloud-only changes before the game loads.
Different progress on both devices asks which version to use. After a
successful in-game save, a worker updates the shared file; network work
never runs in the frame loop. Downloads never replace an active character.
Failed/offline operations preserve local progress and are retried on a
later start. Choosing to play locally without syncing pauses that session's
cloud writes until restart.

Before a local replacement, Cuga writes a complete recovery snapshot and
ordinary PSP/SAVEDATA recovery folders, then stages and swaps save folders.
Before a cloud replacement it backs up the previous cloud container and
extracts its current save into a recovery PSP/SAVEDATA folder. Each cloud
container also embeds the preceding cloud snapshot. Failed backup writes
cancel the replacement. Recovery folders are not automatically pruned.

Use **Open cloud recovery backups** on Windows or **Export cloud recovery
backups...** on Android, then restore a recovery PSP/SAVEDATA folder with
the existing Import save menu and restart. Disconnect syncing while manually
restoring if you want to inspect a recovery save before comparing versions.

The shared container is restricted to hunter save folders ULJM05500,
ULUS10391, and ULES01213. Save paths, file counts and sizes are bounded;
containers have SHA-256 checksums. FUCDAT, game images, settings, loadouts
and logs are not synced. No extra download or third-party app is required
when the cloud provider is already installed.

## Provider behavior

Play on one device at a time. Save, exit Cuga, and allow the cloud provider
to finish syncing before switching. Checking local read-back confirms what
the provider exposes, not delivery to its server or the other device.
Offline caches and simultaneous writers can race; hash comparison is not
a server-side conditional write or distributed lock. Recovery copies are
kept before writes, including failed writes.

The desktop file is updated in place so the cloud document keeps its ID
and the phone's existing permission. A provider can expose a partial write;
invalid containers are rejected rather than replacing local progress.
Provider permissions can be revoked; reconnect through the file picker.
Android document opening and stream readiness use 15-second timeouts.

## Validation

`mhp2g_cloud_save_tests` uses synthetic save files and tests round trips,
path traversal and duplicate rejection, required files, uploads/downloads,
missing-local recovery, conflicts, stale choices, mandatory recovery copies,
offline/read/write failures, partial writes, and changes during an upload.

Windows visual testing exercised the actual startup conflict screen,
cloud selection, backup creation and successful game loading afterward.
An installed-build test also read the real mounted Drive save using an
isolated local save. Android builds and the signed APK were verified;
the phone's Google Drive provider and persistent permission still require
device testing. No measured source coverage percentage was collected.

Android API references: [Storage Access Framework](https://developer.android.com/training/data-storage/shared/documents-files),
[DocumentsProvider](https://developer.android.com/reference/android/provider/DocumentsProvider).
