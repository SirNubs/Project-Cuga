# Security policy

## Reporting a vulnerability

Report suspected security vulnerabilities privately through [GitHub's vulnerability reporting form](https://github.com/TeamGDB/Yakumo/security/advisories/new). Use public issues for ordinary bugs and compatibility problems.

Include the affected version or commit, platform, reproduction steps and expected impact. Relevant areas include processing disc images, saves, mods and texture packs, and ad hoc networking. Provide a minimal synthetic example when possible; do not attach game executables, disc images, game assets, saves containing personal information, credentials or other private data.

Reports are reviewed by the project contributors. Response and fix times depend on their availability; this project does not offer a guaranteed response time. Please coordinate public disclosure with the people handling the report so users can receive a fix first.

## Versions and fixes

Use the latest stable release. Also report problems in current test builds and `main`, identifying the exact commit or release. Older releases may require upgrading to receive a fix.

Fixes go through a pull request into `main` first and are then backported to the relevant release branch when applicable. See [the release policy](docs/RELEASING.md#branches).
