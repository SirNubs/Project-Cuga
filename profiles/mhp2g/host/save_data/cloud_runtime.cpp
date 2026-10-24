#include "save_data/cloud_runtime.hpp"
#include "psprecomp/sha256.hpp"

#include <array>
#include <chrono>
#include <future>
#include <stdexcept>
#if defined(MHP2G_ANDROID_APP)
#include "platform/android_jni.hpp"
#endif

namespace mhp2g::cloud {
namespace {
struct State {
    std::filesystem::path stick, data;
    std::string endpoint, message{"Cloud save is not connected."};
    std::future<Result> work;
    std::optional<Snapshot> pending;
    bool paused{};
};
State &state() { static State s; return s; }
Transport transport(const std::string &endpoint) {
#if defined(MHP2G_ANDROID_APP)
    if (endpoint.starts_with("content://")) {
        const auto read = [endpoint]() -> std::optional<Bytes> {
            auto bytes = android::read_cloud_document(endpoint);
            if (!bytes) throw std::runtime_error("Cannot read the selected cloud file. Check your connection and file permission.");
            return bytes;
        };
        return {read, [endpoint](const Bytes &data) {
            const auto error = android::write_cloud_document(endpoint, data);
            if (!error.empty()) throw std::runtime_error(error);
        }, [read](const Bytes &expected, const std::optional<Bytes> &previous) -> std::optional<Bytes> {
            return confirm_readback(read, expected, previous);
        }};
    }
#endif
    return file_transport(std::filesystem::u8path(endpoint));
}
void launch(Snapshot snapshot, bool download, Choice choice, std::string expected) {
    auto &s = state();
    const auto stick = s.stick, data = s.data;
    const auto endpoint = s.endpoint;
    s.message = "Checking cloud save...";
    s.work = std::async(std::launch::async, [stick, data, endpoint, snapshot = std::move(snapshot), download, choice, expected = std::move(expected)] {
        return synchronize(stick, data, snapshot, transport(endpoint), download, choice, expected);
    });
}
} // namespace
void configure(const std::filesystem::path &stick, const std::filesystem::path &data, const std::string &endpoint) {
    finish();
    auto &s = state();
    s.stick = stick;
    const auto hash = psprecomp::sha256_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(endpoint.data()), endpoint.size()));
    s.data = data / "cloud-sync" / hash.substr(0u, 16u);
    s.endpoint = endpoint;
    s.paused = false;
    s.pending.reset();
    s.message = endpoint.empty() ? "Cloud save is not connected." : "Connected. Cloud progress is checked before the game starts.";
}
bool configured() { return !state().endpoint.empty(); }
bool busy() { return state().work.valid(); }
void begin(bool download, Choice choice, const std::string &expected) {
    if (!configured() || busy()) return;
    try { launch(capture(state().stick), download, choice, expected); }
    catch (const std::exception &e) { state().message = e.what(); }
}
std::optional<Result> poll() {
    auto &s = state();
    if (!s.work.valid() || s.work.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return {};
    Result result;
    try { result = s.work.get(); } catch (const std::exception &e) { result = {Outcome::Error, e.what(), {}}; }
    s.message = result.message;
    if (s.pending && !s.paused) {
        auto snapshot = std::move(*s.pending);
        s.pending.reset();
        launch(std::move(snapshot), false, Choice::Automatic, {});
    }
    return result;
}
std::string status() { return state().message; }
std::filesystem::path backups() { return state().data / "cloud-backups"; }
void save_committed() {
    auto &s = state();
    if (!configured() || s.paused) return;
    try {
        auto snapshot = capture(s.stick); // guest thread: save write has completed
        if (busy()) s.pending = std::move(snapshot);
        else launch(std::move(snapshot), false, Choice::Automatic, {});
    } catch (const std::exception &e) { s.message = e.what(); }
}
void pause_session() { state().paused = true; state().pending.reset(); }
void finish() {
    auto &s = state();
    while (s.work.valid()) {
        s.work.wait();
        (void)poll();
    }
}
} // namespace mhp2g::cloud
