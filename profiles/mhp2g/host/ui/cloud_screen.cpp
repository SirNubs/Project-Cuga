#include "save_data/cloud_runtime.hpp"
#include "save_data/save_transfer.hpp"
#include "settings/settings.hpp"
#include "install/user_data.hpp"
#include "ui/file_browser.hpp"
#include "ui/layer.hpp"
#include "ui/save_screen.hpp"
#include "ui/ui.hpp"
#include "ui/widgets.hpp"
#include "gpu/vulkan_renderer.hpp"
#if defined(MHP2G_ANDROID_APP)
#include "platform/android_jni.hpp"
#include "platform/android_documents.hpp"
#endif

#include <memory>

namespace mhp2g::ui {
namespace {
std::unique_ptr<FileBrowser> browser;
void connect(const std::string &endpoint) {
    auto &s = settings::current();
    s.cloud_file = endpoint;
    s.cloud_auto = true;
    settings::save();
    show_note("Cloud file connected. Restart Cuga to compare saves and start syncing.");
}
}

bool run_cloud_startup() {
    if (!cloud::configured()) return true;
    auto &layer = Layer::get();
    if (!layer.attached()) { cloud::pause_session(); return true; }
    layer.renderer().set_game_input(false);
    layer.set_interactive(true);
    cloud::begin(true);
    bool proceed = false;
    std::optional<cloud::Result> result;
    bool first = true;
    const bool open = layer.run([&] {
        if (auto completed = cloud::poll()) {
            result = *completed;
            if (result->outcome == cloud::Outcome::Unchanged || result->outcome == cloud::Outcome::Uploaded ||
                result->outcome == cloud::Outcome::Downloaded) { proceed = true; return false; }
        }
        begin_panel("##cloud-startup", "Cloud save", "Before loading your character", false);
        begin_content();
        paragraph(cloud::status());
        if (cloud::busy()) {
            paragraph("Checking the shared file. Your local save will be backed up before any replacement.", colors::kTextDim);
        } else {
            paragraph("Backups keep the save being replaced. Play on one device at a time and let Drive finish syncing before switching devices.", colors::kTextDim);
            if (first) { focus_next_row(); first = false; }
            if (button_row("Play this device's save without syncing")) {
                cloud::pause_session(); proceed = true;
            }
            if (result && result->outcome == cloud::Outcome::Conflict) {
                if (button_row("Use this device's save for cloud (back up cloud first)"))
                    cloud::begin(true, cloud::Choice::UseDevice, result->observed_cloud);
                if (button_row("Use the cloud save here (back up this device first)"))
                    cloud::begin(true, cloud::Choice::UseCloud, result->observed_cloud);
            }
            if (button_row("Try cloud check again")) cloud::begin(true);
            if (button_row("Quit")) {
                begin_footer();
                hints({{Control::Confirm, "Select"}});
                end_panel();
                return false;
            }
        }
        begin_footer();
        hints({{Control::Confirm, "Select"}});
        end_panel();
        return !proceed;
    }, false);
    // Never allow the game to race an unfinished startup download.
    if (!open) { cloud::pause_session(); cloud::finish(); }
    layer.set_interactive(false);
    layer.renderer().set_game_input(true);
    return open && proceed;
}

void cloud_rows() {
    auto &s = settings::current();
    section("Cloud saves");
    if (toggle_row("Automatic cloud saves", s.cloud_auto,
            {s.cloud_file.empty(), {}, "Check cloud before starting and send progress after saving. Restart after changing the connection. Backups are mandatory before replacements."})) {
        s.cloud_auto = !s.cloud_auto;
        if (!s.cloud_auto) cloud::pause_session();
        settings::save();
    }
    if (button_row("Connect cloud save...", {cloud::busy(), {},
            "Use the same Project-Cuga.cuga-save file on both devices. On the phone choose it from Google Drive. Restart after connecting."})) {
#if defined(MHP2G_ANDROID_APP)
        if (auto uri = android::pick_cloud_document()) connect(*uri);
#else
        FileBrowser::Options options;
        options.extensions = {".cuga-save"};
        options.filter_name = ".cuga-save";
        options.listed_name = "cloud saves";
        options.empty_note = "Choose a Drive-synced folder to create the shared save file.";
        options.choose_folder = "Use this folder for cloud saves";
        browser = std::make_unique<FileBrowser>(s.cloud_file.empty() ? FileBrowser::home() : std::filesystem::u8path(s.cloud_file).parent_path(), options);
#endif
    }
    info_row("Cloud status", cloud::status());
    if (!s.cloud_file.empty()) info_row("Connection", "Connected; restart after changing settings");
#if defined(MHP2G_ANDROID_APP)
    constexpr const char *backup_label = "Export cloud recovery backups...";
#else
    constexpr const char *backup_label = "Open cloud recovery backups";
#endif
    if (button_row(backup_label, {cloud::busy() || !cloud::configured(), {},
            "Copies the recovery folder somewhere you choose. The PSP/SAVEDATA copies can be restored with Import save."})) {
        try {
            std::filesystem::create_directories(cloud::backups());
#if defined(MHP2G_ANDROID_APP)
            if (auto exported = android::pick_folder_and_copy(cloud::backups()))
                show_note(exported->error.empty() ? "Cloud recovery backups exported." : exported->error);
#else
            open_folder(cloud::backups());
#endif
        } catch (const std::exception &e) { show_note(e.what()); }
    }
    section("Local saves");
}

bool cloud_screen(bool back) {
    if (!browser) return false;
    const auto result = browser->frame(back);
    if (result == FileBrowser::Result::Chosen) {
        auto chosen = browser->chosen();
        if (std::filesystem::is_directory(chosen)) chosen /= "Project-Cuga.cuga-save";
        connect(install::path_to_utf8(chosen));
        browser.reset();
    } else if (result == FileBrowser::Result::Cancelled) browser.reset();
    return true;
}
} // namespace mhp2g::ui
