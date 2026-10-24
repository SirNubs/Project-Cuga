#include "save_data/cloud_save.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace mhp2g::cloud;
int failures{};
void check(bool ok, const char *message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
Snapshot sample(std::uint8_t value) {
    Bytes sfo(20u, 0u);
    sfo[1]='P'; sfo[2]='S'; sfo[3]='F';
    return {{{"ULUS10391/MHP2NDG.BIN", Bytes(100u, value)}, {"ULUS10391/PARAM.SFO", sfo}}};
}
void seed(const fs::path &stick, const Snapshot &s) {
    for (const auto &f : s.files) {
        const auto p = stick / "PSP" / "SAVEDATA" / f.path;
        fs::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(f.data.data()), static_cast<std::streamsize>(f.data.size()));
    }
}
bool rejects(const Snapshot &s) { try { (void)encode(s); return false; } catch (...) { return true; } }
}

int main(int argc, char **argv) {
    using namespace mhp2g::cloud;
    const auto root = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() /
        ("cuga-cloud-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto a = root / "pc", b = root / "phone", sa = root / "pc-state", sb = root / "phone-state";
    const auto cloud_path = root / "drive" / "Project-Cuga.cuga-save";
    auto transport = file_transport(cloud_path);
    const auto first = sample(1u), second = sample(2u), third = sample(3u), fourth = sample(4u);
    check(digest(decode(encode(first))) == digest(first), "snapshot round trip");
    auto bad = first; bad.files[0].path = "../../outside";
    check(rejects(bad), "path traversal refused");
    bad = first; bad.files.push_back(first.files[0]);
    check(rejects(bad), "duplicate refused");
    bad = first; bad.files.pop_back();
    check(rejects(bad), "missing metadata refused");
    bad = first; bad.files[1].data[1] = 0;
    check(rejects(bad), "bad metadata refused");
    for (std::size_t n = 0; n < 12u; ++n) {
        try { (void)decode(Bytes(n, 0)); check(false, "truncated snapshot refused"); } catch (...) {}
    }
    seed(a, first);
    auto result = synchronize(a, sa, capture(a), transport, true);
    check(result.outcome == Outcome::Uploaded, "first PC upload");
    check(synchronize(a, sa, capture(a), transport, true).outcome == Outcome::Unchanged, "no-op sync");
    result = synchronize(b, sb, capture(b), transport, true);
    std::cout << "Initial phone sync: " << result.message << std::endl;
    check(result.outcome == Outcome::Downloaded && digest(capture(b)) == digest(first), "new phone downloads PC save");
    seed(b, second);
    check(synchronize(b, sb, capture(b), transport, false).outcome == Outcome::Uploaded, "phone progress uploads");
    check(synchronize(a, sa, capture(a), transport, false).outcome == Outcome::RemoteAvailable,
        "cloud progress never replaces an active game's save");
    result = synchronize(a, sa, capture(a), transport, true);
    check(result.outcome == Outcome::Downloaded && digest(capture(a)) == digest(second), "PC startup downloads phone progress");
    bool found_backup = false;
    for (const auto &dir : fs::directory_iterator(sa / "cloud-backups")) {
        const auto old = dir.path() / "PSP" / "SAVEDATA" / "ULUS10391" / "MHP2NDG.BIN";
        if (fs::exists(old)) { std::ifstream in(old, std::ios::binary); found_backup |= in.get() == 1; }
    }
    check(found_backup, "overwritten PC save is preserved before download");
    seed(a, third); seed(b, fourth);
    check(synchronize(b, sb, capture(b), transport, false).outcome == Outcome::Uploaded, "other device advances again");
    result = synchronize(a, sa, capture(a), transport, true);
    check(result.outcome == Outcome::Conflict && digest(capture(a)) == digest(third), "divergent progress asks without overwriting");
    check(synchronize(a, sa, capture(a), transport, true, Choice::UseDevice, "stale").outcome == Outcome::Conflict,
        "stale conflict choice refused");
    check(synchronize(a, sa, capture(a), transport, true, Choice::UseDevice, result.observed_cloud).outcome == Outcome::Uploaded,
        "explicit keep device with backup");
    auto fresh_state = root / "fresh-state";
    result = synchronize(b, fresh_state, capture(b), transport, true);
    check(result.outcome == Outcome::Conflict, "first connection with different saves asks");
    check(synchronize(b, fresh_state, capture(b), transport, true, Choice::UseCloud, result.observed_cloud).outcome == Outcome::Downloaded,
        "explicit use cloud with backup");
    const auto bytes = *transport.read();
    const Bytes stale{1u, 2u}, committed{3u, 4u}, another_revision{5u, 6u};
    unsigned confirms = 0;
    const auto immediate_wait = [](int) {};
    auto delayed = confirm_readback([&]() -> std::optional<Bytes> {
        return ++confirms < 3u ? stale : committed;
    }, committed, stale, immediate_wait);
    check(delayed && *delayed == committed && confirms == 3u, "delayed provider read-back settles without rewriting");
    confirms = 0;
    delayed = confirm_readback([&]() -> std::optional<Bytes> { ++confirms; return stale; }, committed, stale, immediate_wait);
    check(delayed && *delayed != committed && confirms == 6u, "permanently stale provider is never confirmed");
    confirms = 0;
    delayed = confirm_readback([&]() -> std::optional<Bytes> { ++confirms; return another_revision; }, committed, stale, immediate_wait);
    check(delayed && *delayed == another_revision && confirms == 1u, "another writer's revision stops confirmation retries");
    confirms = 0;
    delayed = confirm_readback([&]() -> std::optional<Bytes> {
        if (++confirms == 1u) throw std::runtime_error("provider is settling");
        return committed;
    }, committed, stale, immediate_wait);
    check(delayed && *delayed == committed && confirms == 2u, "temporary read failure may settle");
    seed(a, fourth);
    unsigned writes = 0;
    Transport unconfirmed{transport.read, [&](const Bytes &) { ++writes; },
        [&](const Bytes &, const std::optional<Bytes> &) -> std::optional<Bytes> { return bytes; }};
    check(synchronize(a, sa, capture(a), unconfirmed, false).outcome == Outcome::Error && writes == 1u,
        "provider confirmation cannot accept stale data or advance sync baseline");
    seed(a, third);
    auto broken = bytes; broken[20] ^= 1u;
    transport.write(broken);
    check(synchronize(a, sa, capture(a), transport, true).outcome == Outcome::Error && digest(capture(a)) == digest(third),
        "corrupt cloud cannot overwrite local");
    transport.write(bytes);
    Transport offline{[]() -> std::optional<Bytes> { throw std::runtime_error("offline"); }, [](const Bytes &) {}};
    check(synchronize(a, sa, capture(a), offline, true).outcome == Outcome::Error, "offline handled without local change");
    seed(a, fourth);
    Transport fail_write{transport.read, [](const Bytes &) { throw std::runtime_error("write failed"); }};
    check(synchronize(a, sa, capture(a), fail_write, false).outcome == Outcome::Error && digest(capture(a)) == digest(fourth),
        "failed upload preserves local progress");
    unsigned reads = 0;
    bool wrote = false;
    Transport racing{[&]() -> std::optional<Bytes> { return ++reads == 1 ? std::optional<Bytes>(bytes) : std::nullopt; },
        [&](const Bytes &) { wrote = true; }};
    check(synchronize(a, sa, capture(a), racing, false).outcome == Outcome::Error && !wrote,
        "cloud change during backup cancels write");
    Transport short_write{transport.read, [&](const Bytes &data) { transport.write(Bytes(data.begin(), data.begin() + 10)); }};
    check(synchronize(a, sa, capture(a), short_write, false).outcome == Outcome::Error, "partial upload detected");
    transport.write(bytes);
    fs::rename(b / "PSP" / "SAVEDATA" / "ULUS10391", b / "PSP" / "SAVEDATA" / "old-region");
    check(synchronize(b, fresh_state, capture(b), transport, true).outcome == Outcome::Downloaded,
        "missing local save recovers from cloud on an established device");
    fs::rename(cloud_path, cloud_path.string() + ".temporarily-missing");
    check(synchronize(a, sa, capture(a), transport, false).outcome == Outcome::Error, "missing established cloud file is not silently recreated");
    std::cout << (failures ? "FAILED" : "PASS") << ": cloud save synchronization tests; recovery evidence in " << root << '\n';
    return failures ? 1 : 0;
}
