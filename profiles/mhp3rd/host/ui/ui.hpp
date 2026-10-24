#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <array>
#include <optional>
#include <string>

// The port's own interface, drawn with Dear ImGui over the game's window: the
// in-game menu and the first-run setup screens. Only builds with the renderer
// have it.
namespace mhp3rd::gpu {
class VulkanRenderer;
}
namespace mhp3rd::install {
class InstallerUi;
}

namespace mhp3rd::ui {

// Puts the interface on the renderer's window. False when it cannot start;
// the game then runs without a menu.
bool attach(gpu::VulkanRenderer &renderer);

// Before each game frame is presented: draws what the interface shows over
// the running game (the hint that says how to open the menu, the network
// overlay, and the menu when it is open over the running game).
void draw_over_game();

// After a game frame's window events: whether the player asked for the menu
// (Esc, or L3+R3 on a gamepad).
[[nodiscard]] bool menu_requested();

// Saves the game's picture as it was last drawn, at full size, as a PNG in
// screenshots/ in the data directory (#187), and says where for a moment
// over the game. The picture is the game's own: the menu, notes and the
// free camera's line are never in it. Returns the file's path, or an empty
// string with the reason noted when nothing could be taken.
std::string take_screenshot();

// Shows a short note over the game for a few seconds, left out of window
// captures (MHP3RD_INPUT_SCRIPT's shot) like the port's other notes.
void show_note(const std::string &text);

// Where lock-on's marker goes on the game's picture, each coordinate 0..1
// from its top left (camera/lock_on.hpp), or nothing to draw none. Set once
// a flip; draw_over_game() draws it while the game runs.
void set_lock_on_marker(std::optional<std::array<float, 2>> at);

// Whether the menu, opened now, pauses the game. Settings decide: "Pause the
// game when the menu opens", and during ad hoc play "Pause during
// multiplayer", off by default because a paused game stops answering its
// peers.
[[nodiscard]] bool menu_pauses();

// Runs the menu over the last game frame until the player closes it. The
// caller pauses the game around it. False: the player chose to quit.
bool run_menu();

// Opens the menu over the running game instead: draw_over_game() then draws
// it with every game frame, and the game gets no input until it closes.
void open_menu_over_game();
[[nodiscard]] bool menu_over_game();
// Once, after the player chose to quit in a menu over the running game.
[[nodiscard]] bool take_quit_request();

// The reminder to back up the saves (ui/save_screen.cpp). Due once for each
// new release when there are saves, and once after request_backup_reminder(),
// which code about to write or convert saves outside the game calls first
// (with the reason in words for the player). The caller pauses the game around
// run_backup_reminder(), as around run_menu(). False: the window was closed.
[[nodiscard]] bool backup_reminder_due();
bool run_backup_reminder();
void request_backup_reminder(const std::string &reason);

// The setup screens as an installer front end, or null without a window.
std::unique_ptr<install::InstallerUi> make_setup_screens();

// Shows a problem that keeps the game from starting. With ask_setup, offers
// to run the setup again. Unavailable when there is no window to show it in.
enum class ProblemAnswer { Unavailable, Quit, SetUpAgain };
ProblemAnswer show_problem(const std::string &title, const std::string &message, bool ask_setup);

// A question with two answers before the game starts. Unavailable when there
// is no window to ask it in; Closed when the player closed the window or
// went back.
enum class ChoiceAnswer { Unavailable, First, Second, Closed };
ChoiceAnswer ask_choice(
    const std::string &title, const std::string &message, const std::string &first, const std::string &second);

// Runs work on another thread under the setup's progress screen, with a
// Cancel button. work reports through the function it is given, which throws
// install::InstallCancelled once the player cancels. False when there is no
// window to show it in (work has not run then). Exceptions from work reach
// the caller.
using ReportProgress = std::function<void(const std::string &stage, std::uint64_t done, std::uint64_t total)>;
bool run_with_progress(const std::string &title, const std::function<void(const ReportProgress &)> &work);

} // namespace mhp3rd::ui
