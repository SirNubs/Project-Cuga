#include "platform/android_fatal.hpp"

#include "platform/android_documents.hpp"

#include <SDL3/SDL.h>

#include <sys/system_properties.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <system_error>

namespace mhp3rd::android {
namespace {

namespace fs = std::filesystem;

std::string property(const char *name) {
    char value[PROP_VALUE_MAX] = {};
    return __system_property_get(name, value) > 0 ? std::string(value) : std::string();
}

// "Yakumo log 2026-09-28 19-45-03", in local time.
std::string log_folder_name() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    char text[64];
    std::strftime(text, sizeof(text), "Yakumo log %Y-%m-%d %H-%M-%S", &local);
    return text;
}

} // namespace

std::string system_summary() {
    std::string text =
        "Android " + property("ro.build.version.release") + " (API " + std::to_string(SDL_GetAndroidSDKVersion()) + ")";
    const std::string maker = property("ro.product.manufacturer");
    const std::string model = property("ro.product.model");
    if (!maker.empty() || !model.empty()) text += ", " + maker + (maker.empty() ? "" : " ") + model;
    std::string chip = property("ro.soc.model");
    if (chip.empty()) chip = property("ro.board.platform");
    if (!chip.empty()) text += " (" + chip + ")";
    return text;
}

std::optional<SavedLogs> save_logs(const fs::path &storage, const std::string &name) {
    std::fflush(stdout);
    std::fflush(stderr);
    std::error_code ec;
    const fs::path local = storage / "transfer" / name;
    fs::remove_all(local.parent_path(), ec);
    fs::create_directories(local, ec);
    for (const char *file : {"yakumo.log", "yakumo-previous.log", "yakumo-fatal.log"})
        if (fs::exists(storage / file, ec)) fs::copy_file(storage / file, local / file, ec);
    if (fs::is_directory(storage / "logs", ec))
        fs::copy(storage / "logs", local / "logs", fs::copy_options::recursive, ec);
    const std::optional<PickedExport> copied = pick_folder_and_copy(local);
    fs::remove_all(local.parent_path(), ec);
    if (!copied) return std::nullopt;
    return SavedLogs{copied->where + "/" + name, copied->error};
}

void fatal_error(const std::string &title, const std::string &text, int exit_code) {
    // The log first, all of it on disk, and a copy the next start keeps:
    // yakumo.log becomes yakumo-previous.log at the next start and is gone
    // at the one after.
    std::cout << "[fatal] " << text << "\n" << std::flush;
    std::cerr << std::flush;
    std::fflush(stdout);
    std::fflush(stderr);
    const char *storage_text = SDL_GetAndroidInternalStoragePath();
    const fs::path storage = storage_text != nullptr ? fs::path(storage_text) : fs::path();
    if (!storage.empty()) {
        std::error_code ec;
        fs::copy_file(storage / "yakumo.log", storage / "yakumo-fatal.log", fs::copy_options::overwrite_existing, ec);
    }

    enum : int { kSave = 1, kClose = 2 };
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kClose, "Close"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kSave, "Save the log…"},
    };
    std::string note;
    for (;;) {
        // After a save only the outcome is shown: the whole text and the
        // note together push the buttons off a phone's screen.
        const std::string message = note.empty() ? text : note;
        SDL_MessageBoxData box{};
        box.flags = SDL_MESSAGEBOX_ERROR;
        box.title = title.c_str();
        box.message = message.c_str();
        box.numbuttons = storage.empty() ? 1 : 2;
        box.buttons = buttons;
        int pressed = kClose;
        if (!SDL_ShowMessageBox(&box, &pressed)) break;
        if (pressed != kSave) break;
        const std::optional<SavedLogs> saved = save_logs(storage, log_folder_name());
        if (!saved)
            note = "The log was not saved: no folder was chosen.";
        else if (!saved->error.empty())
            note = "The log was not saved: " + saved->error + ".";
        else
            note = "The log was saved to " + saved->where + ".\n\nPlease attach it to your report.";
        std::cout << "[fatal] " << note << "\n" << std::flush;
    }
    std::fflush(stdout);
    std::_Exit(exit_code);
}

} // namespace mhp3rd::android
