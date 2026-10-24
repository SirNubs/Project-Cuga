#include "adhoc/server_status.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace mhp2g::adhoc {
namespace {

using Clock = std::chrono::steady_clock;

struct State {
    std::mutex mutex;
    ServerHalls latest;
    Clock::time_point fetched_at{};
    Clock::time_point asked_at{};
    bool asked{};
    std::atomic<bool> busy{false};
};

State &state() {
    static State *s = new State;
    return *s;
}

// The value of attribute `name` in the tag that starts at `tag`.
std::string attribute(const std::string &xml, std::size_t tag, std::size_t tag_end, const std::string &name) {
    const std::string key = name + "=\"";
    const std::size_t at = xml.find(key, tag);
    if (at == std::string::npos || at > tag_end) return {};
    const std::size_t from = at + key.size();
    const std::size_t to = xml.find('"', from);
    if (to == std::string::npos || to > tag_end) return {};
    return xml.substr(from, to - from);
}

std::vector<HallInfo> parse(const std::string &xml, const std::string &prefix) {
    std::vector<HallInfo> halls;
    std::size_t at = 0;
    while ((at = xml.find("<group ", at)) != std::string::npos) {
        const std::size_t tag_end = xml.find('>', at);
        if (tag_end == std::string::npos) break;
        HallInfo hall;
        hall.group = attribute(xml, at, tag_end, "name");
        const std::string count = attribute(xml, at, tag_end, "usercount");
        hall.players = static_cast<unsigned>(std::strtoul(count.c_str(), nullptr, 10));
        const bool empty_tag = tag_end > 0 && xml[tag_end - 1] == '/';
        std::size_t next = tag_end + 1;
        if (!empty_tag) {
            const std::size_t close = xml.find("</group>", tag_end);
            const std::size_t limit = close == std::string::npos ? xml.size() : close;
            std::size_t user = tag_end;
            while ((user = xml.find("<user>", user)) != std::string::npos && user < limit) {
                const std::size_t from = user + 6;
                const std::size_t to = xml.find("</user>", from);
                if (to == std::string::npos || to > limit) break;
                hall.names.push_back(xml.substr(from, to - from));
                user = to;
            }
            next = limit;
        }
        if (hall.group.size() >= prefix.size() && hall.group.compare(0, prefix.size(), prefix) == 0)
            halls.push_back(std::move(hall));
        at = next;
    }
    std::sort(halls.begin(), halls.end(), [](const HallInfo &a, const HallInfo &b) { return a.group < b.group; });
    return halls;
}

#if defined(_WIN32)
std::wstring widen(const std::string &text) {
    return std::wstring(text.begin(), text.end()); // host names are ASCII
}

// GET https://<server>/status.xml, following the server's redirects. Empty with `error` set on failure.
std::string fetch(const std::string &server, std::string &error) {
    std::string body;
    const HINTERNET session = WinHttpOpen(L"mhp2g-status/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        error = "no HTTP session";
        return body;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 8000);
    const HINTERNET connection = WinHttpConnect(session, widen(server).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    const HINTERNET request = connection == nullptr ? nullptr
        : WinHttpOpenRequest(connection, L"GET", L"/status.xml", nullptr, WINHTTP_NO_REFERER,
              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request != nullptr && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            error = "the server answered " + std::to_string(status);
        } else {
            char chunk[4096];
            DWORD got = 0;
            while (WinHttpReadData(request, chunk, sizeof(chunk), &got) && got != 0 && body.size() < (1u << 20))
                body.append(chunk, got);
            if (body.empty()) error = "the status page is empty";
        }
    } else {
        error = "the server has no status page that can be reached";
    }
    if (request != nullptr) WinHttpCloseHandle(request);
    if (connection != nullptr) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return body;
}
#else
std::string fetch(const std::string &, std::string &error) {
    error = "hall counts are only available in the Windows build";
    return {};
}
#endif

} // namespace

ServerHalls server_halls(const std::string &server, const std::string &group_prefix) {
    State &s = state();
    const Clock::time_point now = Clock::now();
    std::unique_lock lock(s.mutex);
    const bool other_server = s.latest.server != server;
    const bool due = !s.asked || other_server || now - s.asked_at > std::chrono::seconds(10);
    if (due && !server.empty() && !s.busy.exchange(true)) {
        s.asked = true;
        s.asked_at = now;
        if (other_server) s.latest = ServerHalls{};
        s.latest.server = server;
        std::thread([server, group_prefix] {
            std::string error;
            const std::string xml = fetch(server, error);
            std::vector<HallInfo> halls = error.empty() ? parse(xml, group_prefix) : std::vector<HallInfo>{};
            State &shared = state();
            {
                std::lock_guard guard(shared.mutex);
                if (shared.latest.server == server) {
                    shared.latest.fetched = true;
                    shared.latest.error = error;
                    if (error.empty()) shared.latest.halls = std::move(halls);
                    shared.fetched_at = Clock::now();
                }
            }
            shared.busy = false;
        }).detach();
    }
    ServerHalls copy = s.latest;
    copy.age_seconds = s.latest.fetched
        ? static_cast<unsigned>(std::chrono::duration_cast<std::chrono::seconds>(now - s.fetched_at).count())
        : 0u;
    return copy;
}

} // namespace mhp2g::adhoc
