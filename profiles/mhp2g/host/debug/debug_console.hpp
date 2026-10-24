#pragma once

// MHP2G_DEBUG_COMMANDS=<file>: a file of commands for the developer tools,
// read while the game runs like MHP2G_INPUT_LIVE. Each line appended to it is
// run at the next flip and answered with "[debug] ..." lines on the console.
// It is how the game's structures were traced for the Debug page, and how a
// script uses the page's tools without the menu. docs/DEBUG_MENU.md lists the
// commands.

#include <cstdint>
#include <string>
#include <vector>

namespace mhp2g::game {
class Ram;
}

namespace mhp2g::debug {

using game::Ram;

// Runs one command line against guest memory; returns what it printed.
std::vector<std::string> run_command(Ram &ram, const std::string &line);

// At the flip: reads new lines from the command file and runs them.
void console_frame(Ram &ram);

} // namespace mhp2g::debug
