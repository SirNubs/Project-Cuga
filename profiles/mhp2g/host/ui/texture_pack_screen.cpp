#include "ui/texture_pack_screen.hpp"

#include "ui/file_browser.hpp"
#include "ui/layer.hpp"
#include "ui/save_screen.hpp"
#include "ui/widgets.hpp"

#include "gpu/texture_pack_import.hpp"
#include "gpu/vulkan_renderer.hpp"
#include "install/game_identity.hpp"
#include "install/user_data.hpp"
#include "settings/settings.hpp"

#if defined(MHP2G_ANDROID_APP)
#include "platform/document_texture_import.hpp"
#include <array>
#include <cerrno>
#include <fstream>
#include <unistd.h>
#endif

#include "imgui.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace mhp2g::ui {
namespace {

namespace fs = std::filesystem;
using gpu::TexturePackCheck;
using gpu::TexturePackCopy;
using gpu::TexturePackLocation;

enum class Stage { Closed, Choose, Reading, Checking, Review, Copying, Result };

// What the review screen shows: the pack found, and the one in use now.
struct Review {
    TexturePackCheck check;
    TexturePackLocation current;
    gpu::InstalledTexturePack current_pack;
    std::optional<std::uint64_t> free_space;
    bool is_current{}; // the pack chosen is the one in use
};

struct Outcome {
    bool ok{};
    bool in_place{};
    bool cancelled{};
    std::string error;
    fs::path folder; // where the pack is read from now
    fs::path backup; // where the pack it replaced went
    std::size_t keys{};
};

struct State {
    Stage stage{Stage::Closed};
    std::unique_ptr<FileBrowser> browser;
    fs::path last_folder;
    std::future<Review> checking;
    fs::path checking_folder;
    bool cancel_requested{};
    Review review;
    TexturePackCopy copy;
    bool installing{}; // the copy is done; waiting for the renderer to let go of the old pack
    Outcome outcome;
    bool focus{};
    bool focus_row{};
    std::chrono::steady_clock::time_point copy_started;
#if defined(MHP2G_ANDROID_APP)
    fs::path android_staging;
    std::future<std::string> android_read;
#endif
};

State &state() {
    static State s;
    return s;
}

float px(float value) {
    return std::round(value * Layer::get().scale());
}

std::string utf8(const fs::path &path) {
    return install::path_to_utf8(path);
}

gpu::VulkanRenderer &renderer() {
    return Layer::get().renderer();
}

fs::path textures_root() {
    return gpu::VulkanRenderer::textures_root();
}

fs::path installed_folder() {
    return textures_root() / install::kDiscId;
}

void go(Stage stage) {
    State &s = state();
    s.stage = stage;
    s.focus = true;
    ImGui::SetScrollY(0.0f);
}

void close() {
    State &s = state();
    s.stage = Stage::Closed;
    s.browser.reset();
    s.focus_row = true;
#if defined(MHP2G_ANDROID_APP)
    if (s.checking.valid()) s.checking.wait();
    if (!s.android_staging.empty()) {
        std::error_code ec;
        fs::remove_all(s.android_staging, ec);
        s.android_staging.clear();
    }
#endif
}

void focus_first() {
    State &s = state();
    if (s.focus) {
        focus_next_row();
        s.focus = false;
    }
}

void indented(const std::string &text, ImU32 color = colors::kTextDim) {
    ImGui::Indent(px(16.0f));
    paragraph(text, color);
    ImGui::Unindent(px(16.0f));
}

std::string count(std::size_t n, const char *one, const char *many) {
    return std::to_string(n) + " " + (n == 1u ? one : many);
}

void start_check(const fs::path &chosen);
void finish(Outcome outcome);

#if defined(MHP2G_ANDROID_APP)
bool read_texture_document(const std::string &uri, const fs::path &target, std::string &error) {
    const int fd = android::open_document(uri, "r");
    if (fd < 0) {
        error = "Android could not open a texture file. Check the selected folder's access.";
        return false;
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    bool ok = static_cast<bool>(out);
    std::array<char, 65536> buffer{};
    while (ok) {
        const ssize_t bytes = ::read(fd, buffer.data(), buffer.size());
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes <= 0) {
            ok = bytes == 0;
            break;
        }
        out.write(buffer.data(), bytes);
        ok = static_cast<bool>(out);
    }
    ::close(fd);
    out.close();
    ok = ok && !out.fail();
    if (!ok) error = "Could not copy a texture file. Check free space and folder access.";
    return ok;
}

void reading_screen() {
    State &s = state();
    section("Import texture pack");
    indented("Reading the selected texture pack. Large packs can take a while; keep Cuga open.");
    const double t = ImGui::GetTime();
    progress_bar(static_cast<float>(0.5 + 0.5 * std::sin(t * 3.0)), "Reading from Android storage");
    if (s.android_read.valid() &&
        s.android_read.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const std::string error = s.android_read.get();
        if (error.empty()) {
            start_check(s.android_staging);
        } else {
            Outcome outcome;
            outcome.error = error;
            finish(std::move(outcome));
        }
    }
}
#endif

void open_browser() {
    State &s = state();
#if defined(MHP2G_ANDROID_APP)
    // SAF grants access to the selected folder only, without broad storage
    // permission. The renderer later reads an installed private copy.
    if (s.checking.valid()) s.checking.wait();
    if (!s.android_staging.empty()) {
        std::error_code ec;
        fs::remove_all(s.android_staging, ec);
        s.android_staging.clear();
    }
    const auto tree = android::pick_folder();
    if (!tree) {
        close();
        return;
    }
    std::error_code ec;
    const fs::path parent = textures_root() / ".android-import";
    fs::create_directories(parent, ec);
    s.android_staging = parent / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path staging = s.android_staging;
    const std::string uri = android::tree_root(*tree);
    s.android_read = std::async(std::launch::async, [uri, staging] {
        std::string error;
        try {
            const android::TextureDocuments documents{android::list_folder, read_texture_document};
            android::import_texture_documents(documents, uri, staging, error);
        } catch (const std::exception &) {
            error = "Could not read the texture pack. Check folder access and available storage.";
        }
        return error;
    });
    go(Stage::Reading);
    return;
#else
    FileBrowser::Options options;
    options.extensions = {};
    options.filter_name = "folders";
    options.listed_name = "folders";
    options.empty_note = "No folders here.";
    options.choose_folder = "Import from this folder";
    // A pack folder is chosen as soon as it is opened.
    options.choose_on_open = [](const fs::path &folder) {
        std::error_code ec;
        return fs::is_regular_file(folder / "textures.ini", ec);
    };
    fs::path start = s.last_folder;
    if (start.empty()) {
        start = FileBrowser::home() / "Downloads";
        std::error_code ec;
        if (!fs::is_directory(start, ec)) start = FileBrowser::home();
    }
    s.browser = std::make_unique<FileBrowser>(start, std::move(options));
    go(Stage::Choose);
#endif
}

void start_check(const fs::path &chosen) {
    State &s = state();
    const std::string in_place = settings::current().texture_pack_folder;
    // Reading a large pack's folder and textures.ini takes a moment on a slow
    // card; the menu keeps drawing meanwhile.
    s.checking_folder = chosen;
    s.checking = std::async(std::launch::async, [chosen, in_place, root = textures_root()] {
        Review review;
        review.check = gpu::check_texture_pack(chosen, install::kDiscId);
        review.current = gpu::texture_pack_location(root, install::kDiscId, in_place);
        review.current_pack = gpu::summarize_texture_pack(review.current.folder, install::kDiscId);
        review.free_space = gpu::texture_pack_free_space(root);
        std::error_code ec;
        review.is_current = review.check.found() && fs::exists(review.current.folder, ec) &&
            fs::equivalent(review.check.folder, review.current.folder, ec);
        return review;
    });
    go(Stage::Checking);
}

void log_review(const Review &r) {
    const TexturePackCheck &c = r.check;
    std::cout << "[texpack] import from " << utf8(c.chosen) << ": ";
    if (!c.found()) {
        std::cout << "no pack found\n";
        return;
    }
    std::cout << utf8(c.folder) << ", " << c.keys << " keys, " << c.images << " images, " << c.files << " files, "
              << (c.bytes >> 20u) << " MB, " << c.missing << " missing"
              << (c.made_for.empty() ? "" : ", made for " + c.made_for) << (c.ok() ? "" : "; refused: " + c.problem)
              << std::endl;
}

bool browse(bool back) {
    State &s = state();
#if defined(MHP2G_ANDROID_APP)
    indented("Import: open the pack's folder, the one that holds textures.ini, or choose a folder that holds it, "
             "such as PPSSPP's PSP/TEXTURES.");
    indented("Choose the extracted folder containing textures.ini in Android's folder picker.", colors::kTextDim);
#else
    indented("Import: open the pack's folder, the one that holds textures.ini, or choose a folder that holds it, "
             "such as PPSSPP's PSP/TEXTURES. You can also drop the folder on the window.");
#endif
    // A folder dropped on the window is chosen at once.
    if (auto dropped = Layer::get().take_dropped_file()) {
        s.last_folder = s.browser->folder();
        s.browser.reset();
        std::error_code ec;
        start_check(fs::is_directory(*dropped, ec) ? *dropped : dropped->parent_path());
        return true;
    }
    const FileBrowser::Result result = s.browser->frame(back);
    if (result == FileBrowser::Result::Browsing) return true;
    s.last_folder = s.browser->folder();
    const fs::path chosen = s.browser->chosen();
    s.browser.reset();
    if (result == FileBrowser::Result::Cancelled) {
        close();
        return false;
    }
    start_check(chosen);
    return true;
}

void checking_screen(bool back) {
    State &s = state();
    if (back) {
        // The check reads only; let it finish on its own.
        open_browser();
        return;
    }
    section("Import texture pack");
    focus_first();
    indented("Checking " + utf8(s.checking_folder) + "…");
    const double t = ImGui::GetTime();
    progress_bar(static_cast<float>(0.5 + 0.5 * std::sin(t * 3.0)), "");
    if (s.checking.valid() && s.checking.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        s.review = s.checking.get();
        log_review(s.review);
        go(Stage::Review);
    }
}

std::string describe_current(const Review &r) {
    if (!r.current_pack.exists) {
        if (r.current.source == TexturePackLocation::Source::Installed) return "None";
        return "Folder missing: " + utf8(r.current.folder);
    }
    std::string text =
        r.current_pack.problem.empty() ? count(r.current_pack.keys, "texture", "textures") : "Does not load";
    text += ", " + human_size(r.current_pack.bytes);
    return text;
}

// Turns the Texture pack setting on, unless MHP2G_TEXTURE_PACK decides it.
void enable_pack(settings::Settings &settings) {
    if (settings::overridden_by("video.texture_pack") != nullptr) return;
    settings.texture_pack = true;
    renderer().set_texture_pack(true);
}

void finish(Outcome outcome) {
    State &s = state();
    s.outcome = std::move(outcome);
    go(Stage::Result);
#if defined(MHP2G_ANDROID_APP)
    // The checker/copy worker has finished before reaching Result. Release
    // the provider copy now, even if the player exits without pressing Done.
    if (!s.android_staging.empty()) {
        std::error_code ec;
        fs::remove_all(s.android_staging, ec);
        s.android_staging.clear();
    }
#endif
}

void use_in_place() {
    State &s = state();
    settings::Settings &settings = settings::current();
    std::error_code ec;
    const bool is_installed = fs::equivalent(s.review.check.folder, installed_folder(), ec);
    settings.texture_pack_folder = is_installed ? std::string{} : utf8(s.review.check.folder);
    enable_pack(settings);
    settings::save();
    renderer().reload_texture_pack();
    std::cout << "[texpack] using the pack in place: " << utf8(s.review.check.folder) << std::endl;
    Outcome outcome;
    outcome.ok = true;
    outcome.in_place = true;
    outcome.folder = s.review.check.folder;
    outcome.keys = s.review.check.keys;
    finish(std::move(outcome));
}

void start_copy() {
    State &s = state();
    std::error_code ec;
    fs::create_directories(textures_root(), ec);
    s.installing = false;
    s.cancel_requested = false;
    s.copy_started = std::chrono::steady_clock::now();
    s.copy.start(s.review.check, textures_root());
    go(Stage::Copying);
}

void review_screen(bool back) {
    State &s = state();
    if (back) {
        open_browser();
        return;
    }
    const Review &r = s.review;
    const TexturePackCheck &c = r.check;
    section("Import texture pack");
    indented("From " + utf8(c.chosen));
    if (!c.found()) {
        indented(c.problem, colors::kDanger);
    } else {
        indented(c.layout +
            (c.made_for.empty() ? ""
                                : " It is installed as " + std::string(install::kDiscId) +
                        ", the folder name this release reads."));
        section("Pack to import");
        info_row("Folder", utf8(c.folder));
        if (c.keys > 0u) {
            info_row("Textures",
                count(c.keys, "key", "keys") + ", hash " + gpu::texture_pack_hash_name(c.hash) +
                    (c.ignore_address ? ", addresses ignored" : ""));
            info_row("Images",
                count(c.images, "file", "files") + ", " + human_size(c.bytes) + " in all " +
                    count(c.files, "file", "files"));
            if (c.missing > 0u) {
                std::string names;
                for (const std::string &name : c.missing_names) names += (names.empty() ? "" : ", ") + name;
                if (c.missing > c.missing_names.size()) names += ", …";
                info_row("Missing images", std::to_string(c.missing) + " (" + names + ")");
                indented("textures.ini names images that are not in the folder; those textures keep the game's own "
                         "look.");
            }
        }
        if (!c.ok()) indented("Cannot be imported: " + c.problem, colors::kDanger);
        section("In use now");
        info_row(r.current.source == TexturePackLocation::Source::Installed ? "Installed pack" : "Pack folder",
            describe_current(r));
        if (r.current.source != TexturePackLocation::Source::Installed) info_row("Used from", utf8(r.current.folder));
        if (r.current.source == TexturePackLocation::Source::Variable)
            indented("MHP2G_TEXTURE_PACK names this folder, so it stays in use until the variable is unset.",
                colors::kDanger);
    }

    ImGui::Dummy({0.0f, px(12.0f)});
    if (c.ok()) {
        const std::uint64_t needed = c.bytes + gpu::kTexturePackSpaceMargin;
        const bool room = !r.free_space || *r.free_space >= needed;
        std::error_code ec;
        const bool installed_exists = fs::exists(installed_folder(), ec);
        const bool is_installed = r.is_current && r.current.source == TexturePackLocation::Source::Installed;
        // Kept to two lines: the footer has no room for a path.
        std::string copy_note =
            "Copies " + human_size(c.bytes) + " to textures/" + install::kDiscId + " in the data folder";
        copy_note += r.free_space ? " (" + human_size(*r.free_space) + " free)." : std::string(".");
        if (installed_exists) copy_note += " The pack there now moves to textures/.backup; nothing is deleted.";
        if (!room)
            copy_note = "Not enough free space: the copy needs " + human_size(needed) + " and " +
                human_size(*r.free_space) + " is free. Free some space or choose a smaller pack.";
        if (is_installed) copy_note = "This is the installed pack already.";
        const char *label = installed_exists ? "Copy and replace" : "Copy into Project Cuga's data folder";
        // The focus starts on the first thing the player can do; the rows
        // above are a scroll away.
        if (room && !is_installed) focus_first();
        if (button_row(label, {!room || is_installed, {}, copy_note},
                installed_exists ? colors::kAccentBright : colors::kText))
            start_copy();
#if !defined(MHP2G_ANDROID_APP)
        const bool in_use = r.is_current && r.current.source != TexturePackLocation::Source::Variable;
        const std::string place_note = in_use ? "This pack is the one in use already."
                                              : "Reads the pack from its folder and copies nothing, saving " +
                human_size(c.bytes) + ". The folder must stay where it is." +
                (installed_exists ? " The installed pack is kept, unused." : "");
        focus_first();
        if (button_row("Use it where it is", {in_use, {}, place_note})) use_in_place();
#endif
    }
    focus_first();
    if (button_row("Choose another folder", {false, {}, "Back to the folders."})) open_browser();
    if (button_row("Cancel", {false, {}, "Import nothing."})) close();
}

void copying_screen(bool back) {
    State &s = state();
    if (back) {
        s.copy.cancel();
        s.cancel_requested = true;
    }
    const TexturePackCopy::Progress p = s.copy.progress();
    section(s.installing ? "Putting the pack in place" : "Copying texture pack");
    ImGui::Dummy({0.0f, px(6.0f)});
    const float fraction = p.total_bytes > 0u
        ? static_cast<float>(static_cast<double>(p.bytes) / static_cast<double>(p.total_bytes))
        : 0.0f;
    char text[128];
    std::snprintf(text, sizeof(text), "%d%%   %s of %s", static_cast<int>(fraction * 100.0f),
        human_size(p.bytes).c_str(), human_size(p.total_bytes).c_str());
    ImGui::Indent(px(16.0f));
    progress_bar(fraction, text);
    ImGui::Unindent(px(16.0f));
    indented(std::to_string(p.files) + " of " + std::to_string(p.total_files) + " files" +
        (p.current.empty() ? "" : ": " + p.current));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.copy_started).count();
    if (fraction > 0.02f && seconds > 2.0) {
        char eta[64];
        std::snprintf(
            eta, sizeof(eta), "About %d s left.", static_cast<int>(std::ceil(seconds * (1.0 - fraction) / fraction)));
        indented(eta);
    }
    indented("The pack in use now stays as it is until the copy is complete. The menu stays open until then.");
    ImGui::Dummy({0.0f, px(12.0f)});
    focus_first();
    if (button_row(s.cancel_requested ? "Cancelling…" : "Cancel",
            {s.installing || s.cancel_requested, {},
                "Stops the copy and removes what it has copied so far. Nothing else changes."},
            colors::kDanger)) {
        s.copy.cancel();
        s.cancel_requested = true;
    }
}

void result_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    const Outcome &o = s.outcome;
    section("Import texture pack");
    if (o.ok) {
        info_row("Installed", count(o.keys, "texture", "textures") + (o.in_place ? ", used where it is" : ""));
        info_row(o.in_place ? "Used from" : "Installed in", utf8(o.folder));
        if (!o.backup.empty()) info_row("Replaced pack", "Kept in " + utf8(o.backup));
        const std::string status = renderer().texture_pack_status();
        info_row("Texture pack", status);
        indented("The pack is on and applies at once: textures already on screen change within a frame or two.");
    } else if (o.cancelled) {
        info_row("Not imported", "Cancelled");
        indented("Nothing changed: the pack in use before is still in place.");
    } else {
        info_row("Not imported", o.error);
        indented("Nothing changed: the pack in use before is still in place.");
    }
    ImGui::Dummy({0.0f, px(12.0f)});
    if (button_row("Open the textures folder", {false, {}, "Show the folder the pack is in."}))
        open_folder(o.ok && o.in_place ? o.folder : textures_root());
    focus_first();
    if (button_row("Done", {false, {}, "Back to the menu."})) close();
}

// Advances a copy whatever page the menu shows: once it is done, the old
// pack is closed, the folders swap, and the new pack opens.
void tick() {
    State &s = state();
    if (s.stage != Stage::Copying) return;
    switch (s.copy.state()) {
    case TexturePackCopy::State::Copying:
    case TexturePackCopy::State::Idle:
        return;
    case TexturePackCopy::State::Cancelled: {
        s.copy.join();
        Outcome outcome;
        outcome.cancelled = true;
        finish(std::move(outcome));
        return;
    }
    case TexturePackCopy::State::Failed: {
        s.copy.join();
        Outcome outcome;
        outcome.error = s.copy.error();
        finish(std::move(outcome));
        return;
    }
    case TexturePackCopy::State::Done:
        break;
    }
    s.copy.join();
    // The renderer closes the pack at its next frame; the folder moves after.
    if (!s.installing) {
        s.installing = true;
        renderer().hold_texture_pack(true);
    }
    if (!renderer().texture_pack_held()) return;
    Outcome outcome;
    std::string error;
    const fs::path backup_dir = gpu::texture_pack_backup_directory(textures_root(), std::chrono::system_clock::now());
    if (gpu::install_staged_texture_pack(
            s.copy.staging(), textures_root(), install::kDiscId, backup_dir, outcome.backup, error)) {
        settings::Settings &settings = settings::current();
        settings.texture_pack_folder.clear();
        enable_pack(settings);
        settings::save();
        outcome.ok = true;
        outcome.folder = installed_folder();
        outcome.keys = s.review.check.keys;
        std::cout << "[texpack] installed " << utf8(outcome.folder)
                  << (outcome.backup.empty() ? "" : "; the replaced pack is in " + utf8(outcome.backup)) << std::endl;
    } else {
        gpu::discard_staged_texture_pack(s.copy.staging());
        outcome.error = error;
        std::cout << "[texpack] import failed: " << error << std::endl;
    }
    renderer().hold_texture_pack(false);
    renderer().reload_texture_pack();
    s.installing = false;
    finish(std::move(outcome));
}

} // namespace

void texture_pack_rows() {
    State &s = state();
    if (s.focus_row) {
        focus_next_row();
        s.focus_row = false;
    }
    settings::Settings &settings = settings::current();
    if (button_row("Import texture pack…",
            {false, {},
#if defined(MHP2G_ANDROID_APP)
                "Choose the extracted pack folder containing textures.ini in Android's folder picker. "
                "It is checked before installation; the previous pack is backed up."
#else
                "Install an HD texture pack from a folder containing textures.ini or PSP/TEXTURES. "
                "It is checked first; the previous pack is backed up."
#endif
            }))
        open_browser();
    if (button_row("Open the textures folder",
            {false, {}, "Show the textures folder in the data folder, where imported packs go."}))
        open_folder(textures_root());
    if (!settings.texture_pack_folder.empty()) info_row("Pack used from", settings.texture_pack_folder);
    if (!settings.texture_pack_folder.empty() &&
        button_row("Stop using the pack folder",
            {false, {},
                "Go back to the pack installed in the data folder, if any. The pack's own folder is left "
                "alone."})) {
        settings.texture_pack_folder.clear();
        settings::save();
        renderer().reload_texture_pack();
    }
}

bool texture_pack_screen_open() {
    return state().stage != Stage::Closed;
}

bool texture_pack_import_busy() {
    return state().stage == Stage::Copying || state().stage == Stage::Reading;
}

bool texture_pack_screen(bool back) {
    State &s = state();
    switch (s.stage) {
    case Stage::Closed:
        return false;
    case Stage::Choose:
        return browse(back);
    case Stage::Reading:
#if defined(MHP2G_ANDROID_APP)
        reading_screen();
#endif
        break;
    case Stage::Checking:
        checking_screen(back);
        break;
    case Stage::Review:
        review_screen(back);
        break;
    case Stage::Copying:
        copying_screen(back);
        break;
    case Stage::Result:
        result_screen(back);
        break;
    }
    return true;
}

void texture_pack_import_tick() {
    tick();
}

} // namespace mhp2g::ui
