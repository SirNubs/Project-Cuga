// Picks the installer's front end: the port's own setup screens when the
// game's window can be created, the SDL dialogs otherwise.

#include "install/installer.hpp"
#include "install/user_data.hpp"

#if defined(MHP3RD_HAS_RENDERER)
#include "ui/ui.hpp"
#endif

#include <filesystem>
#include <iostream>
#include <string>

namespace mhp3rd::install {

std::unique_ptr<InstallerUi> make_installer_ui() {
#if defined(MHP3RD_HAS_RENDERER)
    if (auto screens = ui::make_setup_screens()) return screens;
#endif
    return make_dialog_ui();
}

bool report_problem(const std::string &title, const std::string &message, bool ask_setup) {
    std::cerr << title << ": " << message << "\n";
#if defined(MHP3RD_HAS_RENDERER)
    switch (ui::show_problem(title, message, ask_setup)) {
    case ui::ProblemAnswer::Quit:
        return false;
    case ui::ProblemAnswer::SetUpAgain:
        return true;
    case ui::ProblemAnswer::Unavailable:
        break;
    }
#endif
    return report_problem_in_dialog(title, message, ask_setup);
}

namespace {

std::string copy_summary(std::uint64_t files, const std::filesystem::path &to) {
    return "Copied " + std::to_string(files) + (files == 1u ? " file" : " files") + " into " + path_to_utf8(to);
}

// Copies with the progress screen when there is a window, on the console
// otherwise. False when the player cancelled; throws when the copy failed.
bool copy_with_progress(const std::filesystem::path &from, const std::filesystem::path &to) {
    std::uint64_t files = 0;
#if defined(MHP3RD_HAS_RENDERER)
    try {
        const bool shown = ui::run_with_progress("Copying your data", [&](const ui::ReportProgress &report) {
            files = copy_user_data(from, to,
                [&report](std::uint64_t done, std::uint64_t total) { report("Copying your data", done, total); });
        });
        if (shown) {
            std::cout << "[data] " << copy_summary(files, to) << std::endl;
            return true;
        }
    } catch (const InstallCancelled &) {
        std::cout << "[data] copy cancelled; nothing was kept" << std::endl;
        return false;
    }
#endif
    files = copy_user_data(
        from, to, [](std::uint64_t done, std::uint64_t total) { print_progress("Copying your data", done, total); });
    std::cout << "[data] " << copy_summary(files, to) << std::endl;
    return true;
}

} // namespace

bool offer_user_data_copy(const std::filesystem::path &from, const std::filesystem::path &to) {
    const std::string title = "Use your existing data?";
    const std::string message = "This copy of Yakumo is portable: it keeps its settings, saves and game data in\n" +
        path_to_utf8(to) + "\n\nwhich is empty. Yakumo is also set up on this computer, with its data in\n" +
        path_to_utf8(from) +
        "\n\nCopy that data here to keep playing with your saves and settings? The original stays where it is "
        "and is not changed.";
    const std::string copy = "Copy my data";
    const std::string fresh = "Start empty";
    int answer = -1;
#if defined(MHP3RD_HAS_RENDERER)
    switch (ui::ask_choice(title, message, copy, fresh)) {
    case ui::ChoiceAnswer::First:
        answer = 1;
        break;
    case ui::ChoiceAnswer::Second:
        answer = 2;
        break;
    case ui::ChoiceAnswer::Closed:
        answer = 0;
        break;
    case ui::ChoiceAnswer::Unavailable:
        break;
    }
#endif
    if (answer < 0) answer = ask_choice_in_dialog(title, message, copy, fresh);
    if (answer < 0) {
        std::cout << "[data] " << path_to_utf8(to) << " is empty; to copy the data in " << path_to_utf8(from)
                  << " into it, run Yakumo with --copy-user-data (add --portable or --data-dir as for this run)"
                  << std::endl;
        return true;
    }
    if (answer == 0) return false;
    if (answer == 2) return true;
    try {
        (void)copy_with_progress(from, to);
    } catch (const std::exception &e) {
        (void)report_problem("Copy failed",
            std::string("Yakumo could not copy your data:\n") + e.what() +
                "\n\nWhat it had copied was removed again; the original is unchanged.",
            false);
        return false;
    }
    return true;
}

int copy_user_data_on_console(const std::filesystem::path &from, const std::filesystem::path &to) {
    std::error_code ec;
    if (from.empty() || !has_user_data(from)) {
        std::cerr << "Yakumo: no data to copy in " << path_to_utf8(from) << "\n";
        return 1;
    }
    if (std::filesystem::equivalent(from, to, ec)) {
        std::cerr << "Yakumo: " << path_to_utf8(to) << " is already the data folder in use\n";
        return 1;
    }
    try {
        const std::uint64_t files = copy_user_data(from, to,
            [](std::uint64_t done, std::uint64_t total) { print_progress("Copying your data", done, total); });
        std::cout << copy_summary(files, to) << "; " << path_to_utf8(from) << " is unchanged.\n";
    } catch (const std::exception &e) {
        std::cerr << "Yakumo: copy failed: " << e.what() << "\nWhat was copied has been removed again.\n";
        return 1;
    }
    return 0;
}

} // namespace mhp3rd::install
