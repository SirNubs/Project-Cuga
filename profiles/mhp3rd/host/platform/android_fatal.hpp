#pragma once

// What the Android app does when it cannot go on (the GPU cannot draw, the
// device was lost). A phone has no console, and a player who sees only a
// dialog can send only a screenshot of it, so the dialog says what failed
// together with the phone, its Android version and the GPU, and offers to
// save the log through the document picker before the app closes. Only in
// the Android app (MHP3RD_ANDROID_APP).

#include <filesystem>
#include <optional>
#include <string>

namespace mhp3rd::android {

// "Android 14 (API 34), Infinix X6832 (mt6789)": what a report needs about
// the phone, read from the system properties.
[[nodiscard]] std::string system_summary();

// Flushes and keeps the log (yakumo-fatal.log beside yakumo.log, which the
// next start does not replace), shows `text` with Save the log… and Close,
// and ends the process with `exit_code` once the player closes it.
[[noreturn]] void fatal_error(const std::string &title, const std::string &text, int exit_code);

struct SavedLogs {
    std::string where; // the picked folder and the log folder's name
    std::string error; // empty: saved
};
// Copies Yakumo's logs in `storage` (this run's, the previous run's, the
// last fatal error's and the logs folder) into a folder named `name` in a
// folder the player picks. Nothing when the player cancels.
[[nodiscard]] std::optional<SavedLogs> save_logs(const std::filesystem::path &storage, const std::string &name);

} // namespace mhp3rd::android
