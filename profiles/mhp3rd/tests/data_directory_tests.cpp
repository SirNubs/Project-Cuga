// Where the data directory is, and copying an installed copy's data into a
// portable folder. Works in a temporary directory; no game data.
#include "install/user_data.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace mhp3rd::install;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint64_t file_count(const fs::path &root) {
    std::uint64_t count = 0;
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file()) ++count;
    return count;
}

void test_portable_detection(const fs::path &root) {
    const fs::path exe = root / "detect";
    fs::create_directories(exe);
    check(portable_data_directory(exe, false, false).empty(), "a plain folder is not portable");
    check(portable_data_directory(exe, true, false) == exe / "data", "--portable uses data/ next to the executable");
    check(portable_data_directory({}, true, false).empty(), "no executable folder, no portable folder");

    write(exe / kPortableMarkerFile, "");
    check(portable_data_directory(exe, false, false) == exe / "data", "portable.txt switches portable mode on");
    check(portable_data_directory(exe, false, true).empty(), "MHP3RD_PORTABLE=0 switches it off again");
    fs::remove(exe / kPortableMarkerFile);

    fs::create_directories(exe / "data");
    check(portable_data_directory(exe, false, false) == exe / "data", "a data folder switches portable mode on");

    // A file called data is not the data folder.
    const fs::path other = root / "detect-file";
    write(other / "data", "not a folder");
    check(portable_data_directory(other, false, false).empty(), "a file named data does not switch it on");
}

void test_writable(const fs::path &root) {
    const fs::path fresh = root / "writable" / "nested" / "data";
    check(check_writable(fresh).empty(), "a new folder is created and written");
    check(fs::is_directory(fresh), "the folder exists afterwards");
    check(fs::is_empty(fresh), "the check leaves nothing behind");

    // A path under a file can never be created.
    write(root / "blocker", "x");
    check(!check_writable(root / "blocker" / "data").empty(), "a folder that cannot be created is reported");
}

void test_has_user_data(const fs::path &root) {
    const fs::path dir = root / "has";
    fs::create_directories(dir);
    check(!has_user_data(dir), "an empty folder holds no data");
    write(dir / "pipeline_cache.bin", "cache");
    check(!has_user_data(dir), "a pipeline cache alone is not data worth copying");
    write(dir / "ms0" / "PSP" / "SAVEDATA" / "x", "save");
    check(has_user_data(dir), "saves are data");
    check(has_user_data(root / "has-settings") == false, "a missing folder holds no data");
    write(root / "has-settings" / kSettingsFile, "audio.mute=0\n");
    check(has_user_data(root / "has-settings"), "settings are data");
}

void test_copy(const fs::path &root) {
    const fs::path from = root / "installed";
    const fs::path to = root / "portable" / "data";
    write(from / kSettingsFile, "disc_image=disc.iso\n");
    write(from / kExecutableFile, std::string(3u << 20, 'e')); // more than one buffer
    write(from / "ms0" / "PSP" / "SAVEDATA" / "ULJM05800" / "MHP3RD.BIN", "save");
    fs::create_directories(from / "logs");
    write(to / kSettingsFile, "kept=1\n");

    std::uint64_t last_done = 0;
    std::uint64_t last_total = 0;
    bool monotonic = true;
    const std::uint64_t copied = copy_user_data(from, to, [&](std::uint64_t done, std::uint64_t total) {
        if (done < last_done) monotonic = false;
        last_done = done;
        last_total = total;
    });
    check(copied == 2u, "the two files missing in the target are copied");
    check(read(to / kSettingsFile) == "kept=1\n", "a file already in the target is never replaced");
    check(read(to / kExecutableFile) == read(from / kExecutableFile), "a large file arrives whole");
    check(read(to / "ms0" / "PSP" / "SAVEDATA" / "ULJM05800" / "MHP3RD.BIN") == "save", "saves are copied");
    check(fs::is_directory(to / "logs"), "empty folders are copied too");
    check(monotonic && last_done == last_total && last_total == (3u << 20) + 4u, "progress counts the bytes");
    check(read(from / kSettingsFile) == "disc_image=disc.iso\n" && file_count(from) == 3u,
        "the source is left as it was");
    check(copy_user_data(from, to) == 0u, "copying again copies nothing");

    // Stopping half way removes what was copied and leaves no partial file.
    const fs::path stopped = root / "stopped";
    write(stopped / "keep.txt", "mine");
    bool threw = false;
    try {
        (void)copy_user_data(from, stopped, [](std::uint64_t done, std::uint64_t) {
            if (done > (1u << 20)) throw std::runtime_error("cancel");
        });
    } catch (const std::runtime_error &) {
        threw = true;
    }
    check(threw, "a progress function can stop the copy");
    check(file_count(stopped) == 1u && read(stopped / "keep.txt") == "mine",
        "a stopped copy removes what it copied and keeps what was there");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "yakumo-data-directory-tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    try {
        test_portable_detection(root);
        test_writable(root);
        test_has_user_data(root);
        test_copy(root);
    } catch (const std::exception &e) {
        ++failures;
        std::cerr << "FAIL: unexpected exception: " << e.what() << '\n';
    }
    fs::remove_all(root, ec);
    if (failures != 0) {
        std::cerr << failures << " data directory check(s) failed\n";
        return 1;
    }
    std::cout << "data directory tests passed\n";
    return 0;
}
