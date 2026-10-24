// Checks that the host reads and writes files under folder names outside
// ASCII: a user folder in Cyrillic, a disc image in a Japanese folder. No
// game data is needed.
//
//   mhp3rd_path_tests
//
// Everything happens under <temp>/mhp3rd_path_tests_<n>/Юникод_テスト, which
// is removed afterwards; the data directory is pointed there with
// MHP3RD_DATA_DIR. The names are written as universal character names, so the
// test means the same whatever encoding the compiler reads this file in. On
// Windows these names are outside the ANSI code page of most systems, which is
// what broke path::string() and path(std::string) there.
#include "fonts/game_font.hpp"
#include "gpu/screenshot.hpp"
#include "gpu/texture_pack.hpp"
#include "gpu/texture_pack_import.hpp"
#include "install/user_data.hpp"
#include "kernel/iso_image.hpp"
#include "platform/utf8_path.hpp"
#include "save_data/savedata_store.hpp"
#include "save_data/save_transfer.hpp"
#include "settings/settings.hpp"

#include "psprecomp/common.hpp"
#include "psprecomp/elf32.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using mhp3rd::path_from_utf8;
using mhp3rd::path_to_utf8;

namespace {

// Юникод_テスト
constexpr const char8_t *kFolder = u8"\u042E\u043D\u0438\u043A\u043E\u0434_\u30C6\u30B9\u30C8";
// Мои игры, ゲーム
constexpr const char8_t *kGames = u8"\u041C\u043E\u0438 \u0438\u0433\u0440\u044B, \u30B2\u30FC\u30E0";
// Паки テクスチャ
constexpr const char8_t *kPacks = u8"\u041F\u0430\u043A\u0438 \u30C6\u30AF\u30B9\u30C1\u30E3";
// кнопка
constexpr const char8_t *kImage = u8"\u043A\u043D\u043E\u043F\u043A\u0430";
// Шрифт フォント.ttf
constexpr const char8_t *kFont = u8"\u0428\u0440\u0438\u0444\u0442 \u30D5\u30A9\u30F3\u30C8.ttf";
// Сохранения
constexpr const char8_t *kExports = u8"\u0421\u043E\u0445\u0440\u0430\u043D\u0435\u043D\u0438\u044F";

int failures = 0;

void check(bool condition, const std::string &what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition) ++failures;
}

// UTF-8 text from a u8 literal.
std::string text(const char8_t *utf8) {
    const std::u8string value(utf8);
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

void write(const fs::path &path, const std::string &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

std::string read(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool contains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

// Sets an environment variable to UTF-8 text, as a player's shell would.
void set_environment(const char *name, const std::string &value) {
#if defined(_WIN32)
    _wputenv_s(mhp3rd::widen(name).c_str(), mhp3rd::widen(value).c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

struct Scratch {
    fs::path base;
    fs::path root;
    Scratch() {
        std::random_device random;
        base = fs::temp_directory_path() / ("mhp3rd_path_tests_" + std::to_string(random()));
        root = base / kFolder;
        fs::create_directories(root);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
};

void test_conversions(const fs::path &root) {
    const std::string utf8 = text(kFolder);
    check(path_to_utf8(fs::path(kFolder)) == utf8, "a path converts to UTF-8");
    check(path_from_utf8(utf8) == fs::path(kFolder), "UTF-8 converts to the same path");
    check(path_from_utf8(path_to_utf8(root)) == root, "a full path round-trips through UTF-8");
    check(path_to_utf8(root.filename()) == utf8, "the folder's name comes back as it was");
    check(fs::is_directory(path_from_utf8(path_to_utf8(root))), "the round-tripped path names the folder");

    set_environment("MHP3RD_PATH_TESTS_VALUE", path_to_utf8(root));
    check(mhp3rd::environment_path("MHP3RD_PATH_TESTS_VALUE") == root, "an environment variable gives the path back");
    check(mhp3rd::environment_utf8("MHP3RD_PATH_TESTS_VALUE").value_or("") == path_to_utf8(root),
        "an environment variable reads as UTF-8");
    check(!mhp3rd::environment_utf8("MHP3RD_PATH_TESTS_UNSET"), "an unset variable has no value");
    check(mhp3rd::environment_path("MHP3RD_PATH_TESTS_UNSET").empty(), "an unset variable names no path");

    const std::string url = mhp3rd::folder_url(root);
#if defined(_WIN32)
    check(path_from_utf8(url) == fs::path(root).make_preferred(), "the folder opens by its own path on Windows");
#else
    check(url.rfind("file://", 0) == 0 && url.find("%D0%AE") != std::string::npos,
        "the folder URL percent-encodes UTF-8");
#endif
}

void test_settings(const fs::path &data_dir, const fs::path &pack_folder, const fs::path &font) {
    namespace install = mhp3rd::install;
    namespace settings = mhp3rd::settings;
    check(install::user_data_directory() == data_dir, "MHP3RD_DATA_DIR names the data directory");

    // The disc image the installer records: a path outside the data directory.
    const fs::path image = data_dir.parent_path() / kGames / "MHP3rd HD.iso";
    install::UserSettings recorded;
    recorded.disc_image = image;
    install::save_settings(data_dir, recorded);
    check(contains(read(data_dir / install::kSettingsFile), "disc_image=" + path_to_utf8(image)),
        "settings.ini holds the disc image's path in UTF-8");
    check(install::load_settings(data_dir).disc_image == image, "the disc image's path reads back");

    // The player's settings, next to the installer's key.
    write(data_dir / install::kExecutableFile, "ELF");
    install::SettingsEntries entries = install::read_settings_file(data_dir);
    entries["text.font"] = path_to_utf8(font);
    install::write_settings_file(data_dir, entries);
    check(settings::current().font == path_to_utf8(font), "a font path in settings.ini loads as UTF-8");
    settings::current().texture_pack_folder = path_to_utf8(pack_folder);
    settings::save();
    const std::string saved = read(data_dir / install::kSettingsFile);
    check(contains(saved, "video.texture_pack_folder=" + path_to_utf8(pack_folder)),
        "a folder set in the menu is saved in UTF-8");
    check(contains(saved, "text.font=" + path_to_utf8(font)), "the font's path is saved as it was read");
    check(contains(saved, "disc_image=" + path_to_utf8(image)), "saving keeps the installer's key");
    check(!fs::exists(data_dir / (std::string(install::kSettingsFile) + ".part")), "no partial settings file is left");

    const auto installed = install::find_installation(data_dir);
    check(installed && installed->disc_image == image && installed->executable == data_dir / "EBOOT.ELF",
        "the installation is found in the data directory");
    const auto location =
        mhp3rd::gpu::texture_pack_location(data_dir / "textures", "NPJB40001", settings::current().texture_pack_folder);
    check(location.folder == pack_folder, "a pack used in place is found from its setting");
}

void test_saves(const fs::path &root) {
    namespace sd = mhp3rd::savedata;
    const fs::path stick = root / "ms0";
    sd::SaveFiles files;
    files.game_name = "ULJM05800";
    files.file_name = "MHP3RD.BIN";
    files.key = sd::Block{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    sd::SaveContents contents;
    contents.data = std::vector<std::uint8_t>(4096u, 0x5Au);
    contents.title = "Title";
    contents.icon0 = {1, 2, 3};
    std::string error;
    check(sd::write_save(stick, files, contents, error), "a save is written under the folder (" + error + ")");
    const fs::path folder = sd::save_folder(stick, files);
    check(fs::is_regular_file(folder / "MHP3RD.BIN") && fs::is_regular_file(folder / "PARAM.SFO"),
        "the save's files are there");
    bool leftover = false;
    for (const auto &entry : fs::directory_iterator(folder)) leftover = leftover || entry.path().extension() == ".tmp";
    check(!leftover, "no .tmp file is left beside the save");
    contents.data.assign(4096u, 0xA5u);
    check(sd::write_save(stick, files, contents, error), "the save is written again over the first one");
    const sd::LoadResult loaded = sd::load_save(stick, files);
    check(loaded.status == sd::LoadStatus::Ok && loaded.contents.data == contents.data, "the save loads back");

    // Export to a folder the player picked, and import from there.
    const auto time = std::chrono::system_clock::from_time_t(1790000000);
    const fs::path exports = root / kExports;
    fs::create_directories(exports);
    const sd::ExportResult exported = sd::export_saves(stick, exports, time);
    check(exported.ok && exported.folder.parent_path() == exports, "the saves are exported (" + exported.error + ")");
    const auto found = sd::find_saves(exported.folder, files.key);
    check(found.size() == 1u && found.front().ok(), "the exported save is found and passes its checks");
    if (found.size() == 1u) {
        const fs::path other = root / kGames / "ms0";
        const sd::ImportResult imported =
            sd::import_save(found.front(), other, sd::backup_directory(sd::savedata_root(other), time));
        check(imported.ok, "the save is imported into another memory stick (" + imported.error + ")");
        check(sd::load_save(other, files).contents.data == contents.data, "the imported save loads");
    }
}

void test_texture_pack(const fs::path &root, const fs::path &chosen) {
    namespace gpu = mhp3rd::gpu;
    const std::string image = text(kImage) + ".png";
    write(chosen / "textures.ini",
        "[options]\nversion = 1\nhash = xxh64\nignoreAddress = true\n\n[hashes]\n"
        "0000000022585cbda625131a = ui/" +
            image + "\n");
    write(chosen / "ui" / path_from_utf8(image), "png");
    write(chosen / "000000001111111122222222.png", "hash-named");

    gpu::TexturePackInfo info;
    std::string error;
    check(gpu::TexturePack::inspect(chosen, "NPJB40001", info, error), "the pack reads (" + error + ")");
    check(std::find(info.files.begin(), info.files.end(), "ui/" + image) != info.files.end(),
        "the image textures.ini names keeps its name");

    const gpu::TexturePackCheck checked = gpu::check_texture_pack(chosen, "NPJB40001");
    check(checked.ok() && checked.missing == 0u, "the pack passes the import's checks (" + checked.problem + ")");
    const fs::path textures = root / "textures";
    gpu::TexturePackCopy copy;
    copy.start(checked, textures);
    copy.join();
    check(copy.state() == gpu::TexturePackCopy::State::Done, "the pack is copied (" + copy.error() + ")");
    fs::path backup;
    const bool installed = copy.state() == gpu::TexturePackCopy::State::Done &&
        gpu::install_staged_texture_pack(copy.staging(), textures, "NPJB40001",
            gpu::texture_pack_backup_directory(textures, std::chrono::system_clock::now()), backup, error);
    check(installed, "the copy is put in place (" + error + ")");
    check(read(textures / "NPJB40001" / "ui" / path_from_utf8(image)) == "png", "the image arrives with its name");
    const gpu::InstalledTexturePack summary = gpu::summarize_texture_pack(textures / "NPJB40001", "NPJB40001");
    check(summary.exists && summary.problem.empty(), "the installed pack loads (" + summary.problem + ")");
}

// A font the system has, copied under the data directory; empty when none.
fs::path copy_a_font(const fs::path &fonts) {
    const char *const candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Geneva.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    for (const char *candidate : candidates) {
        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec)) continue;
        fs::create_directories(fonts, ec);
        const fs::path copied = fonts / kFont;
        if (fs::copy_file(candidate, copied, fs::copy_options::overwrite_existing, ec)) return copied;
    }
    return {};
}

void test_font(const fs::path &font) {
    namespace fonts = mhp3rd::fonts;
    if (font.empty()) {
        std::printf("skip the font checks: no known system font to copy\n");
        return;
    }
    fonts::reload();
    check(fonts::problem().empty(), "the chosen font opens from the folder (" + fonts::problem() + ")");
    check(fonts::ready(), "a font is ready");
    check(fonts::metrics('A').found, "the font has a glyph for A");
    check(fonts::user_font_folder() == path_to_utf8(font.parent_path()), "the fonts folder is named in UTF-8");

    fonts::start_catalog();
    bool done = false;
    std::vector<fonts::FontChoice> choices;
    for (int i = 0; i < 600 && !done; ++i) {
        choices = fonts::catalog(done);
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const bool listed = std::any_of(choices.begin(), choices.end(), [&](const fonts::FontChoice &choice) {
        return choice.user_folder && choice.path == path_to_utf8(font) && choice.value == path_to_utf8(font);
    });
    check(done && listed, "the font in the fonts folder is listed with its UTF-8 path");
}

// A disc image with the smallest ISO 9660 structure IsoImage reads: the
// primary volume descriptor and a root directory holding one file.
std::string tiny_iso() {
    constexpr std::size_t kSector = 2048u;
    std::string image(20u * kSector, '\0');
    const auto le32 = [&](std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) image[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFFu);
    };
    const std::size_t pvd = 16u * kSector;
    image[pvd] = 1;
    image.replace(pvd + 1u, 5u, "CD001");
    le32(pvd + 156u + 2u, 18u);      // root directory's sector
    le32(pvd + 156u + 10u, kSector); // and size
    const std::size_t record = 18u * kSector;
    const std::string name = "README.TXT;1";
    image[record] = static_cast<char>(34u + name.size());
    le32(record + 2u, 19u);
    le32(record + 10u, 5u);
    image[record + 32u] = static_cast<char>(name.size());
    image.replace(record + 33u, name.size(), name);
    image.replace(19u * kSector, 5u, "hello");
    return image;
}

void test_disc_image_and_executable(const fs::path &root) {
    const fs::path folder = root / kGames;
    const fs::path iso = folder / "MHP3rd HD.iso";
    write(iso, tiny_iso());
    try {
        mhp3rd::IsoImage image(iso);
        check(image.find("README.TXT").has_value() && image.size_bytes() == 20u * 2048u,
            "a disc image in the folder opens and reads");
    } catch (const std::exception &e) {
        check(false, std::string("a disc image in the folder opens: ") + e.what());
    }
    const fs::path not_iso = folder / (text(kImage) + ".iso");
    write(not_iso, std::string(40u * 1024u, 'x'));
    try {
        mhp3rd::IsoImage image(not_iso);
        check(false, "a file that is not a disc image is refused");
    } catch (const psprecomp::Error &e) {
        check(contains(e.what(), path_to_utf8(not_iso)), "a refused disc image is named in UTF-8");
    } catch (const std::exception &e) {
        check(false, std::string("a refused disc image fails with its own message, not: ") + e.what());
    }

    const fs::path elf = root / "EBOOT.ELF";
    write(elf, std::string(64u, '\0'));
    check(psprecomp::sha256_file(elf).size() == 64u, "the executable's hash is read from the folder");
    try {
        (void)psprecomp::Elf32Image::from_file(elf);
        check(false, "a file that is not an ELF is refused");
    } catch (const psprecomp::Error &) {
        check(true, "a file that is not an ELF is refused by the ELF reader, not by a path conversion");
    } catch (const std::exception &e) {
        check(false, std::string("a file that is not an ELF is refused by the ELF reader, not: ") + e.what());
    }
    try {
        (void)psprecomp::sha256_file(root / "missing.elf");
        check(false, "a missing executable is reported");
    } catch (const psprecomp::Error &e) {
        check(contains(e.what(), path_to_utf8(root)), "a missing executable is named in UTF-8");
    } catch (const std::exception &e) {
        check(false, std::string("a missing executable is reported by the reader, not: ") + e.what());
    }
}

// The data folder chosen through MHP3RD_DATA_DIR, a portable data\ folder
// beside the executable, and screenshots written into the data folder.
void test_data_folders(const fs::path &root, const fs::path &data_dir) {
    check(mhp3rd::install::data_directory().path == data_dir &&
            mhp3rd::install::data_directory().source == mhp3rd::install::DataSource::Environment,
        "MHP3RD_DATA_DIR names the data folder");
    const fs::path program = root / kGames / "Yakumo";
    fs::create_directories(program);
    check(mhp3rd::install::portable_data_directory(program, false, false).empty(), "no portable folder by itself");
    {
        std::ofstream(program / mhp3rd::install::kPortableMarkerFile) << "\n";
    }
    const fs::path portable = mhp3rd::install::portable_data_directory(program, false, false);
    check(portable == program / mhp3rd::install::kPortableDataFolder, "portable.txt keeps the data in data/ beside it");
    check(mhp3rd::install::check_writable(portable).empty(), "and that folder can be written");
    check(mhp3rd::screenshot::folder() == data_dir / "screenshots", "screenshots go into the data folder");
    const std::vector<std::uint8_t> rgba(2u * 2u * 4u, 0x80u);
    std::string error;
    const fs::path shot = mhp3rd::screenshot::free_path(mhp3rd::screenshot::folder(), std::chrono::system_clock::now());
    check(mhp3rd::screenshot::write_png(shot, rgba, 2u, 2u, error) && fs::is_regular_file(shot),
        "a screenshot is written there: " + error);
}

} // namespace

int main() {
    try {
        const Scratch scratch;
        const fs::path data_dir = scratch.root / "Yakumo" / "MHP3rd";
        fs::create_directories(data_dir);
        set_environment("MHP3RD_DATA_DIR", path_to_utf8(data_dir));
        // Variables that would override what the test writes to settings.ini.
#if defined(_WIN32)
        _wputenv_s(L"MHP3RD_FONT", L"");
        _wputenv_s(L"MHP3RD_TEXTURE_PACK", L"");
#else
        unsetenv("MHP3RD_FONT");
        unsetenv("MHP3RD_TEXTURE_PACK");
#endif

        test_conversions(scratch.root);
        const fs::path font = copy_a_font(data_dir / "fonts");
        const fs::path pack = scratch.root / kPacks / "NPJB40001";
        test_settings(data_dir, pack, font.empty() ? data_dir / "fonts" / kFont : font);
        test_saves(scratch.root);
        test_texture_pack(data_dir, pack);
        test_font(font);
        test_disc_image_and_executable(scratch.root);
        test_data_folders(scratch.root, data_dir);
    } catch (const std::exception &e) {
        std::printf("FAIL unexpected exception: %s\n", e.what());
        ++failures;
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
