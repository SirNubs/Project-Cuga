#include "save_data/savedata_store.hpp"

#include "save_data/param_sfo.hpp"
#include "save_data/savedata_crypto.hpp"

#include "platform/utf8_path.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <vector>
#include <chrono>
#include <iostream>

namespace mhp2g::savedata {
namespace {

constexpr const char *kParamSfo = "PARAM.SFO";
// SAVEDATA_FILE_LIST: 99 entries of a 13-byte name, its 16-byte hash and 3
// bytes of padding.
constexpr std::size_t kFileListEntries = 99u;
constexpr std::size_t kFileListEntrySize = 32u;
constexpr std::size_t kFileListNameSize = 13u;
// The mode a PSP picks for a game built with SDK 4 or later that passes a
// key, which is what this game does.
constexpr CryptMode kSaveMode = CryptMode::Mode5;

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool write_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    // Write beside the target and rename, so an interrupted save never leaves
    // a half-written file in place of a good one.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    return !ec;
}

// A hash stored in the file list for `name`, if any.
std::optional<Block> listed_hash(const ParamSfo &sfo, const std::string &name) {
    const auto *list = sfo.binary("SAVEDATA_FILE_LIST");
    if (list == nullptr) return std::nullopt;
    for (std::size_t offset = 0; offset + kFileListEntrySize <= list->size(); offset += kFileListEntrySize) {
        std::string entry_name;
        for (std::size_t i = 0; i < kFileListNameSize && (*list)[offset + i] != 0u; ++i)
            entry_name.push_back(static_cast<char>((*list)[offset + i]));
        if (entry_name != name) continue;
        Block hash{};
        std::copy_n(list->begin() + static_cast<std::ptrdiff_t>(offset + kFileListNameSize), hash.size(), hash.begin());
        return hash;
    }
    return std::nullopt;
}

Block random_block() {
    std::random_device device;
    std::mt19937_64 engine(
        device() ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    Block block{};
    for (auto &b : block) b = static_cast<std::uint8_t>(engine());
    return block;
}

} // namespace

std::filesystem::path savedata_root(const std::filesystem::path &memory_stick) {
    return memory_stick / "PSP" / "SAVEDATA";
}

std::filesystem::path save_folder(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    return savedata_root(memory_stick) / (files.game_name + files.save_name);
}

bool save_exists(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    std::error_code ec;
    const auto folder = save_folder(memory_stick, files);
    return std::filesystem::is_directory(folder, ec) && std::filesystem::is_regular_file(folder / kParamSfo, ec);
}

LoadResult load_save(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    LoadResult result;
    const auto folder = save_folder(memory_stick, files);
    if (!save_exists(memory_stick, files)) return result;
    const auto sfo_bytes = read_file(folder / kParamSfo);
    const auto sfo = sfo_bytes ? ParamSfo::parse(*sfo_bytes) : std::nullopt;
    const auto file = read_file(folder / files.file_name);
    if (!file) return result; // the folder exists but the game's file does not
    result.status = LoadStatus::Broken;
    if (!sfo) {
        result.reason = "PARAM.SFO is unreadable";
        return result;
    }

    const auto *params = sfo->binary("SAVEDATA_PARAMS");
    const std::uint8_t flags = params != nullptr && !params->empty() ? (*params)[kParamsFlagsOffset] : 0u;
    if (flags != 0u) {
        auto mode = mode_from_flags(flags);
        if (!mode) {
            result.reason = "unknown SAVEDATA_PARAMS flags " + std::to_string(flags);
            return result;
        }
        const Block *key = files.key ? &*files.key : nullptr;
        if (*mode != CryptMode::Mode1 && key == nullptr) {
            result.reason = "the save is encrypted with a game key but the game passed none";
            return result;
        }

        // Outlives the hash check below: `key` may come to point at it.
        std::optional<Block> other_key;
        const auto expected = listed_hash(*sfo, files.file_name);

        if (expected) {
            const auto hash_start = std::chrono::steady_clock::now();

            auto actual = data_file_hash(*file, *mode, key);
            // The flags can name another mode than the one the data was written with (PPSSPP picks
            // the mode from the SDK version and retries on load). The hash says which one fits.
            if (*expected != actual) {
                for (const CryptMode other : {CryptMode::Mode3, CryptMode::Mode5, CryptMode::Mode1}) {
                    if (other == *mode || (other != CryptMode::Mode1 && key == nullptr)) continue;
                    const auto retried = data_file_hash(*file, other, key);
                    if (retried != *expected) continue;
                    std::cerr << "[savedata] " << files.file_name << " matches its hash as mode "
                              << static_cast<int>(other) << ", not mode " << static_cast<int>(*mode) << " as flagged" << std::endl;
                    mode = other;
                    actual = retried;
                    break;
                }
            }

            const auto hash_end = std::chrono::steady_clock::now();

            std::cerr << "[savedata-perf] data_file_hash="
                    << std::chrono::duration<double, std::milli>(
                            hash_end - hash_start
                        ).count()
                    << " ms\n";

            // Still no match: the save was written with another game key. MHP2G under the FUC
            // patch passes the Japanese game's key here, while a save brought over from PPSSPP
            // or a PSP that the US game (or FUC there) wrote carries the US game's. The hash
            // itself says which key made it, so that key opens the data; the game then checks
            // what comes out, and the next save is written with the game's own key again.
            if (*expected != actual && *mode != CryptMode::Mode1) {
                other_key = implied_game_key(*file, *mode, *expected);
                std::cerr << "[savedata] " << files.file_name
                          << " was written with another game key; using the key its hash carries" << std::endl;
                key = &*other_key;
                actual = *expected;
            }
            if (*expected != actual) {
                result.reason = files.file_name +
                                " does not match its hash in PARAM.SFO";
                return result;
            }
        }

        const auto decrypt_start = std::chrono::steady_clock::now();

        auto plain = decrypt_data(*file, *mode, key);

        const auto decrypt_end = std::chrono::steady_clock::now();

        std::cerr << "[savedata-perf] decrypt_data="
                << std::chrono::duration<double, std::milli>(
                        decrypt_end - decrypt_start
                    ).count()
                << " ms\n";

        if (!plain) {
            result.reason = files.file_name + " is too short to be encrypted";
            return result;
        }
        result.contents.data = std::move(*plain);
    } else {
        result.contents.data = *file;
    }
    result.contents.title = sfo->string("TITLE").value_or("");
    result.contents.savedata_title = sfo->string("SAVEDATA_TITLE").value_or("");
    result.contents.detail = sfo->string("SAVEDATA_DETAIL").value_or("");
    result.contents.parental_level = sfo->integer("PARENTAL_LEVEL").value_or(0u);
    result.status = LoadStatus::Ok;
    return result;
}

namespace {

// How many dated copies of a save are kept, and how close together two may be.
constexpr std::size_t kSaveBackups = 20u;
constexpr auto kSaveBackupSpacing = std::chrono::seconds(60);

// After a save has been written: a dated copy of its folder under PSP/SAVEDATA_BACKUP/<save>/, next
// to the saves so it travels with them, and the oldest copies beyond kSaveBackups removed. A port
// this young should not be one bad write away from losing a character. Failing here never fails
// the save: the copy is extra.
void keep_backup(const std::filesystem::path &memory_stick, const std::filesystem::path &folder) {
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (last != std::chrono::steady_clock::time_point{} && now - last < kSaveBackupSpacing) return;
    std::error_code ec;
    const std::filesystem::path root = memory_stick / "PSP" / "SAVEDATA_BACKUP" / folder.filename();
    const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
    const std::filesystem::path target = root / stamp;
    std::filesystem::create_directories(target, ec);
    if (ec) return;
    std::filesystem::copy(folder, target,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "[savedata] no backup made in " << path_to_utf8(target) << ": " << ec.message() << std::endl;
        return;
    }
    last = now;
    // The names sort by date; everything in here was made by this function.
    std::vector<std::filesystem::path> copies;
    for (const auto &entry : std::filesystem::directory_iterator(root, ec))
        if (entry.is_directory(ec)) copies.push_back(entry.path());
    std::sort(copies.begin(), copies.end());
    for (std::size_t i = 0; i + kSaveBackups < copies.size(); ++i) std::filesystem::remove_all(copies[i], ec);
}

} // namespace

bool write_save(const std::filesystem::path &memory_stick, const SaveFiles &files, const SaveContents &contents,
    std::string &error) {
    const auto folder = save_folder(memory_stick, files);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        error = "cannot create " + path_to_utf8(folder) + ": " + ec.message();
        return false;
    }

    // Keep what an existing PARAM.SFO lists for other files.
    std::optional<ParamSfo> previous;
    if (const auto bytes = read_file(folder / kParamSfo)) previous = ParamSfo::parse(*bytes);
    std::vector<std::uint8_t> file_list(kFileListEntries * kFileListEntrySize, 0u);
    if (previous) {
        if (const auto *list = previous->binary("SAVEDATA_FILE_LIST"); list != nullptr)
            std::copy_n(list->begin(), std::min(list->size(), file_list.size()), file_list.begin());
    }

    std::vector<std::uint8_t> stored;
    if (files.key) {
        stored = encrypt_data(contents.data, kSaveMode, &*files.key, random_block());
        const Block hash = data_file_hash(stored, kSaveMode, &*files.key);
        // Replace this file's entry, or take the first free one.
        std::size_t slot = file_list.size();
        for (std::size_t offset = 0; offset < file_list.size(); offset += kFileListEntrySize) {
            const std::string name(reinterpret_cast<const char *>(&file_list[offset]),
                strnlen(reinterpret_cast<const char *>(&file_list[offset]), kFileListNameSize));
            if (name == files.file_name) {
                slot = offset;
                break;
            }
            if (name.empty() && slot == file_list.size()) slot = offset;
        }
        if (slot == file_list.size()) {
            error = "SAVEDATA_FILE_LIST is full";
            return false;
        }
        std::fill_n(file_list.begin() + static_cast<std::ptrdiff_t>(slot), kFileListEntrySize, 0u);
        std::copy_n(files.file_name.begin(), std::min(files.file_name.size(), kFileListNameSize - 1u),
            file_list.begin() + static_cast<std::ptrdiff_t>(slot));
        std::copy(hash.begin(), hash.end(), file_list.begin() + static_cast<std::ptrdiff_t>(slot + kFileListNameSize));
    } else {
        stored = contents.data;
    }

    ParamSfo sfo;
    sfo.set_string("CATEGORY", "MS", 4u);
    sfo.set_integer("PARENTAL_LEVEL", contents.parental_level);
    sfo.set_string("SAVEDATA_DETAIL", contents.detail, 1024u);
    sfo.set_string("SAVEDATA_DIRECTORY", files.game_name + files.save_name, 64u);
    sfo.set_binary("SAVEDATA_FILE_LIST", file_list, static_cast<std::uint32_t>(file_list.size()));
    sfo.set_binary("SAVEDATA_PARAMS", std::vector<std::uint8_t>(kParamsSize, 0u), kParamsSize);
    sfo.set_string("SAVEDATA_TITLE", contents.savedata_title, 128u);
    sfo.set_string("TITLE", contents.title, 128u);
    std::vector<std::uint8_t> sfo_bytes = sfo.serialize();
    if (files.key) sign_param_sfo(sfo_bytes, *sfo.data_offset("SAVEDATA_PARAMS"), kSaveMode);

    const std::pair<const char *, const std::vector<std::uint8_t> *> media[] = {{"ICON0.PNG", &contents.icon0},
        {"ICON1.PMF", &contents.icon1}, {"PIC1.PNG", &contents.pic1}, {"SND0.AT3", &contents.snd0}};
    for (const auto &[name, bytes] : media) {
        if (bytes->empty()) continue;
        if (!write_file(folder / name, *bytes)) {
            error = std::string("cannot write ") + name;
            return false;
        }
    }
    // The data file first and PARAM.SFO last: PARAM.SFO carries the data
    // file's hash, so a save interrupted in between is reported as broken
    // rather than loaded with mismatched contents.
    if (!write_file(folder / files.file_name, stored)) {
        error = "cannot write " + files.file_name;
        return false;
    }
    if (!write_file(folder / kParamSfo, sfo_bytes)) {
        error = "cannot write PARAM.SFO";
        return false;
    }
    keep_backup(memory_stick, folder);
    return true;
}

bool delete_save(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    std::error_code ec;
    const auto removed = std::filesystem::remove_all(save_folder(memory_stick, files), ec);
    return !ec && removed > 0u;
}

std::uint64_t save_size(const std::filesystem::path &memory_stick, const SaveFiles &files) {
    std::error_code ec;
    std::uint64_t total = 0u;
    const auto folder = save_folder(memory_stick, files);
    if (!std::filesystem::is_directory(folder, ec)) return 0u;
    for (const auto &entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.is_regular_file(ec)) total += entry.file_size(ec);
    return total;
}

} // namespace mhp2g::savedata
