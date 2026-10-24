#include "save_data/cloud_save.hpp"
#include "psprecomp/sha256.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace mhp2g::cloud {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t kMax = 24u * 1024u * 1024u;
constexpr std::array<const char *, 3> kRegions{"ULJM05500", "ULUS10391", "ULES01213"};
constexpr std::array<const char *, 6> kFiles{"MHP2NDG.BIN", "PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3"};
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
bool safe_path(const std::string &path) {
    for (const auto *region : kRegions)
        for (const auto *file : kFiles)
            if (path == std::string(region) + "/" + file) return true;
    return false;
}
Bytes read_file(const fs::path &path) {
    require(!fs::is_symlink(path), "Refusing a symbolic link in save data.");
    const auto size = fs::file_size(path);
    require(size <= kMax, "Save file is too large.");
    Bytes bytes(static_cast<std::size_t>(size));
    std::ifstream in(path, std::ios::binary);
    if (size != 0u) in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    require(static_cast<bool>(in), "Cannot read the complete save file.");
    return bytes;
}
void write_file(const fs::path &path, const Bytes &bytes) {
    require(!fs::is_symlink(path), "Refusing a symbolic link in save data.");
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    require(static_cast<bool>(out), "Cannot finish writing the save file.");
}
void number(Bytes &out, std::size_t n) {
    require(n <= kMax, "Invalid cloud save length.");
    for (unsigned i = 0; i < 4u; ++i) out.push_back(static_cast<std::uint8_t>(n >> (i * 8u)));
}
std::size_t number(const Bytes &in, std::size_t &at) {
    require(at <= in.size() && in.size() - at >= 4u, "Truncated cloud save.");
    std::size_t n = 0;
    for (unsigned i = 0; i < 4u; ++i) n |= static_cast<std::size_t>(in[at++]) << (i * 8u);
    require(n <= kMax, "Invalid cloud save length.");
    return n;
}
Bytes take(const Bytes &in, std::size_t &at, std::size_t count) {
    require(at <= in.size() && count <= in.size() - at, "Truncated cloud save.");
    Bytes out(in.begin() + static_cast<std::ptrdiff_t>(at), in.begin() + static_cast<std::ptrdiff_t>(at + count));
    at += count;
    return out;
}
void validate(const Snapshot &s) {
    require(s.files.size() <= kRegions.size() * kFiles.size(), "Too many files in cloud save.");
    std::set<std::string> paths;
    std::size_t total = 0;
    for (const auto &f : s.files) {
        require(safe_path(f.path) && paths.insert(f.path).second, "Invalid or duplicate cloud save path.");
        total += f.data.size();
        require(total <= kMax / 2u, "Cloud save exceeds its size limit.");
    }
    for (const auto *region : kRegions) {
        const std::string prefix = std::string(region) + "/";
        bool any = false;
        for (const auto &f : s.files) if (f.path.starts_with(prefix)) any = true;
        if (any) {
            require(paths.contains(prefix + "MHP2NDG.BIN") && paths.contains(prefix + "PARAM.SFO"),
                "Cloud save is missing its data or metadata.");
            const auto sfo = std::find_if(s.files.begin(), s.files.end(), [&](const File &f) { return f.path == prefix + "PARAM.SFO"; });
            require(sfo->data.size() >= 20u && sfo->data[0] == 0u && sfo->data[1] == 'P' && sfo->data[2] == 'S' && sfo->data[3] == 'F',
                "Cloud save metadata is not a PSP save.");
            const auto data = std::find_if(s.files.begin(), s.files.end(), [&](const File &f) { return f.path == prefix + "MHP2NDG.BIN"; });
            require(!data->data.empty(), "Cloud save data is empty.");
        }
    }
}
Bytes envelope(const Bytes &current, const Bytes &previous) {
    Bytes out{'C','U','G','A','C','L','D','1'};
    number(out, current.size());
    out.insert(out.end(), current.begin(), current.end());
    number(out, previous.size());
    out.insert(out.end(), previous.begin(), previous.end());
    const auto hash = psprecomp::sha256_bytes(out);
    out.insert(out.end(), hash.begin(), hash.end());
    return out;
}
struct CloudFile { Bytes current; Bytes previous; };
CloudFile unpack(const Bytes &bytes) {
    require(bytes.size() >= 80u && bytes.size() <= kMax, "Invalid cloud save container size.");
    require(std::equal(bytes.begin(), bytes.begin() + 8, std::array<std::uint8_t,8>{'C','U','G','A','C','L','D','1'}.begin()), "Not a Project Cuga cloud save.");
    const std::size_t end = bytes.size() - 64u;
    const auto hash = psprecomp::sha256_bytes(std::span<const std::uint8_t>(bytes.data(), end));
    require(std::equal(hash.begin(), hash.end(), bytes.begin() + static_cast<std::ptrdiff_t>(end)), "Cloud save checksum failed; local save was not replaced.");
    std::size_t at = 8u;
    const auto n = number(bytes, at);
    auto current = take(bytes, at, n);
    const auto old = number(bytes, at);
    auto previous = take(bytes, at, old);
    require(at == end, "Unexpected cloud save contents.");
    (void)decode(current);
    if (!previous.empty()) (void)decode(previous);
    return {std::move(current), std::move(previous)};
}
void text_file(const fs::path &path, const std::string &text) {
    write_file(path, Bytes(text.begin(), text.end()));
}
std::string text_file(const fs::path &path) {
    if (!fs::exists(path)) return {};
    const auto bytes = read_file(path);
    return std::string(bytes.begin(), bytes.end());
}
fs::path fresh_directory(const fs::path &parent, const std::string &prefix) {
    fs::create_directories(parent);
    for (unsigned i = 0u; i < 10000u; ++i) {
        const auto path = parent / (prefix + "-" + std::to_string(i));
        if (fs::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot create a unique save backup directory.");
}
std::string stamp() {
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
void write_snapshot(const fs::path &root, const Snapshot &snapshot) {
    for (const auto &f : snapshot.files) write_file(root / fs::path(f.path), f.data);
}
void install(const fs::path &stick, const fs::path &state_dir, const Snapshot &incoming, const Snapshot &local) {
    const fs::path saves = stick / "PSP" / "SAVEDATA";
    require(!fs::is_symlink(stick) && !fs::is_symlink(stick / "PSP") && !fs::is_symlink(saves), "Refusing a symbolic save directory.");
    const auto backup = fresh_directory(state_dir / "cloud-backups", "before-download-" + stamp());
    // Complete, verified recovery snapshot BEFORE moving any local save.
    write_file(backup / "device.cuga-snapshot", encode(local));
    write_snapshot(backup / "PSP" / "SAVEDATA", local);
    const auto stage = fresh_directory(saves, ".cloud-stage-" + stamp());
    write_snapshot(stage, incoming);
    std::vector<fs::path> moved, installed;
    try {
        for (const auto *region : kRegions) {
            const auto dest = saves / region;
            require(!fs::is_symlink(dest), "Refusing a symbolic save directory.");
            if (fs::exists(dest)) {
                fs::rename(dest, stage / (std::string("old-") + region));
                moved.push_back(region);
            }
            if (fs::exists(stage / region)) {
                fs::rename(stage / region, dest);
                installed.push_back(region);
            }
        }
    } catch (...) {
        for (const auto &region : installed) fs::rename(saves / region, stage / region);
        for (const auto &region : moved) fs::rename(stage / ("old-" + region.string()), saves / region);
        throw;
    }
    // Original directories (including unrecognized extra files) are retained.
    for (const auto &region : moved) fs::rename(stage / ("old-" + region.string()), backup / ("original-" + region.string()));
    fs::remove(stage); // empty on success; never a recursive deletion
}
} // namespace

Snapshot capture(const std::filesystem::path &stick) {
    Snapshot s;
    const auto root = stick / "PSP" / "SAVEDATA";
    require(!fs::is_symlink(stick) && !fs::is_symlink(stick / "PSP") && !fs::is_symlink(root), "Refusing a symbolic save directory.");
    for (const auto *region : kRegions) {
        require(!fs::is_symlink(root / region), "Refusing a symbolic save directory.");
        if (!fs::exists(root / region)) continue;
        for (const auto *file : kFiles)
            if (fs::exists(root / region / file)) s.files.push_back({std::string(region) + "/" + file, read_file(root / region / file)});
    }
    validate(s);
    return s;
}
Bytes encode(const Snapshot &snapshot) {
    validate(snapshot);
    auto files = snapshot.files;
    std::sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.path < b.path; });
    Bytes out{'C','U','G','A','S','N','P','1'};
    number(out, files.size());
    for (const auto &f : files) {
        number(out, f.path.size());
        out.insert(out.end(), f.path.begin(), f.path.end());
        number(out, f.data.size());
        out.insert(out.end(), f.data.begin(), f.data.end());
    }
    return out;
}
Snapshot decode(const Bytes &bytes) {
    require(bytes.size() >= 12u && bytes.size() <= kMax / 2u, "Invalid snapshot size.");
    require(std::equal(bytes.begin(), bytes.begin() + 8, std::array<std::uint8_t,8>{'C','U','G','A','S','N','P','1'}.begin()), "Invalid save snapshot.");
    std::size_t at = 8u;
    const auto count = number(bytes, at);
    require(count <= 18u, "Too many cloud save files.");
    Snapshot s;
    for (std::size_t i = 0u; i < count; ++i) {
        const auto path_length = number(bytes, at);
        require(path_length <= 48u, "Invalid cloud save path.");
        const auto path = take(bytes, at, path_length);
        const auto length = number(bytes, at);
        s.files.push_back({std::string(path.begin(), path.end()), take(bytes, at, length)});
    }
    require(at == bytes.size(), "Unexpected snapshot contents.");
    validate(s);
    return s;
}
std::string digest(const Snapshot &snapshot) { return psprecomp::sha256_bytes(encode(snapshot)); }

Transport file_transport(const std::filesystem::path &path) {
    return {[path]() -> std::optional<Bytes> {
        if (!fs::exists(path)) return std::nullopt;
        return read_file(path);
    }, [path](const Bytes &bytes) {
        // Update the same document in place so Drive retains its document ID
        // and the phone's persistent permission. Recovery is saved first.
        write_file(path, bytes);
    }};
}

std::optional<Bytes> confirm_readback(const std::function<std::optional<Bytes>()> &read,
    const Bytes &expected, const std::optional<Bytes> &previous, const std::function<void(int)> &wait) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    std::optional<Bytes> observed;
    for (const int delay : {0, 250, 750, 1500, 2500, 4000}) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        if (wait) wait(delay);
        else std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        try {
            observed = read();
            if (observed && *observed == expected) return observed;
            if (observed != previous) return observed; // a different revision, never overwrite it
        } catch (const std::exception &) {
            // A transient provider failure may settle after its normal close.
        }
    }
    return observed;
}

Result synchronize(const fs::path &stick, const fs::path &state_dir, const Snapshot &local,
    const Transport &transport, bool allow_download, Choice choice, const std::string &expected_cloud) {
    try {
        validate(local);
        fs::create_directories(state_dir);
        const auto remote = transport.read();
        const auto packed = remote ? unpack(*remote) : CloudFile{};
        const auto cloud = remote ? decode(packed.current) : Snapshot{};
        const std::string local_hash = digest(local);
        const std::string cloud_hash = remote ? digest(cloud) : "missing";
        const auto baseline = text_file(state_dir / "baseline");
        const auto separator = baseline.find('\n');
        const std::string base_local = separator == std::string::npos ? "" : baseline.substr(0, separator);
        const std::string base_cloud = separator == std::string::npos ? "" : baseline.substr(separator + 1);
        if (choice != Choice::Automatic && expected_cloud != cloud_hash)
            return {Outcome::Conflict, "Cloud save changed again. Check both versions before choosing.", cloud_hash};
        const auto remember = [&](const std::string &l, const std::string &c) { text_file(state_dir / "baseline", l + "\n" + c); };
        if (remote && local_hash == cloud_hash) {
            remember(local_hash, cloud_hash);
            return {Outcome::Unchanged, "Cloud save is up to date.", cloud_hash};
        }
        if (local.files.empty() && !remote) return {Outcome::Unchanged, "No saved character to sync yet.", cloud_hash};
        bool upload = choice == Choice::UseDevice;
        bool download = choice == Choice::UseCloud;
        if (choice == Choice::Automatic) {
            if (!remote) {
                if (!base_cloud.empty() && base_cloud != "missing")
                    return {Outcome::Error, "Cloud file is unavailable or was removed. Local save is safe; reconnect before syncing.", cloud_hash};
                upload = !local.files.empty();
            } else if (local.files.empty()) download = true;
            else if (!base_local.empty() && local_hash == base_local && cloud_hash != base_cloud) download = true;
            else if (!base_cloud.empty() && cloud_hash == base_cloud && local_hash != base_local) upload = true;
            else return {Outcome::Conflict, "Both device and cloud have different progress. Choose which save to use; the replaced save will be backed up.", cloud_hash};
        }
        if (download) {
            if (!allow_download) return {Outcome::RemoteAvailable, "Cloud progress changed. Restart Cuga to review it before playing further.", cloud_hash};
            require(remote.has_value() && !cloud.files.empty(), "No usable cloud save to download.");
            require(digest(capture(stick)) == local_hash, "Device save changed during sync; nothing was replaced.");
            install(stick, state_dir, cloud, local);
            remember(cloud_hash, cloud_hash);
            return {Outcome::Downloaded, "Cloud save loaded. The replaced device save was backed up.", cloud_hash};
        }
        if (upload) {
            require(!local.files.empty(), "An empty device save will not replace cloud progress.");
            const auto current = encode(local);
            const auto outgoing = envelope(current, packed.current);
            const auto backup = fresh_directory(state_dir / "cloud-backups", "before-upload-" + stamp());
            write_file(backup / "device.cuga-snapshot", current);
            if (remote) {
                write_file(backup / "previous-cloud.cuga-save", *remote);
                write_snapshot(backup / "previous-cloud" / "PSP" / "SAVEDATA", cloud);
            }
            // A second read catches changes while the local backup was made.
            require(transport.read() == remote, "Cloud save changed during sync; upload cancelled.");
            transport.write(outgoing);
            const auto verified = transport.confirm ? transport.confirm(outgoing, remote) : transport.read();
            require(verified && *verified == outgoing, "Cloud write could not be verified. Local save and recovery backup are safe.");
            remember(local_hash, local_hash);
            return {Outcome::Uploaded, "Cloud save uploaded and verified. Previous progress was backed up.", local_hash};
        }
        return {Outcome::Error, "No cloud sync action was selected.", cloud_hash};
    } catch (const std::exception &e) { return {Outcome::Error, e.what(), {}}; }
}
} // namespace mhp2g::cloud
