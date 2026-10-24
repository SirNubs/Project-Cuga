#pragma once

#include <string>
#include <vector>

// The status page a public PSP ad hoc server publishes (https://<server>/status.xml, as socom.cc
// does): every game, its groups and who is in them. The matchmaking protocol itself only lists
// group names, so this is where the number of players in each hall comes from.
//
// Fetched on a thread of its own, at most once every ten seconds, and only while something asks
// for it. Windows only (WinHTTP, which brings TLS); elsewhere it reports that it is unavailable.
namespace mhp2g::adhoc {

struct HallInfo {
    std::string group; // e.g. MHP2Q003
    unsigned players{};
    std::vector<std::string> names;
};

struct ServerHalls {
    bool fetched{};       // a fetch has finished, well or not
    std::string error;    // why the last fetch failed; empty when it worked
    std::string server;   // the server the list is for
    unsigned age_seconds{};
    std::vector<HallInfo> halls; // groups whose name starts with the prefix asked for, by name
};

// Asks for a fresh list for `server` (host name, without a port) if the last one is older than
// ten seconds, and returns the latest one there is.
ServerHalls server_halls(const std::string &server, const std::string &group_prefix);

} // namespace mhp2g::adhoc
