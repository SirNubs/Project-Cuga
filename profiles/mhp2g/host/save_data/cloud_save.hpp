#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mhp2g::cloud {
using Bytes = std::vector<std::uint8_t>;
struct File { std::string path; Bytes data; };
struct Snapshot { std::vector<File> files; };
enum class Choice { Automatic, UseDevice, UseCloud };
enum class Outcome { Unchanged, Uploaded, Downloaded, Conflict, RemoteAvailable, Error };
struct Result {
    Outcome outcome{Outcome::Error};
    std::string message;
    std::string observed_cloud;
};
// Transport read: missing file = nullopt; I/O failure throws. Writes must
// close completely before returning. The engine verifies by reading back.
struct Transport {
    std::function<std::optional<Bytes>()> read;
    std::function<void(const Bytes &)> write;
    // Providers may expose the previous cached contents briefly after close.
    // This may wait for read-back, but must never rewrite or accept stale data.
    std::function<std::optional<Bytes>(const Bytes &, const std::optional<Bytes> &)> confirm;
};
Snapshot capture(const std::filesystem::path &memory_stick);
Bytes encode(const Snapshot &snapshot);
Snapshot decode(const Bytes &bytes);
std::string digest(const Snapshot &snapshot);
Transport file_transport(const std::filesystem::path &path);
std::optional<Bytes> confirm_readback(const std::function<std::optional<Bytes>()> &read,
    const Bytes &expected, const std::optional<Bytes> &previous,
    const std::function<void(int)> &wait = {});
// Never call while the game is saving. Downloads are only permitted before
// the game loads its first save. Both sides of a replacement are backed up
// locally, and the cloud container retains its previous save as well.
Result synchronize(const std::filesystem::path &memory_stick, const std::filesystem::path &state_dir,
    const Snapshot &local, const Transport &transport, bool allow_download,
    Choice choice = Choice::Automatic, const std::string &expected_cloud = {});
} // namespace mhp2g::cloud
