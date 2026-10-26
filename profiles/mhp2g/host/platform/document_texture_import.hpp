#pragma once

#include "platform/android_jni.hpp"

#include <filesystem>
#include <functional>
#include <set>
#include <string>

namespace mhp2g::android {

// The provider supplies document URIs, never filesystem paths. Kept separate
// from JNI so the same traversal can be checked with an in-memory provider.
struct TextureDocuments {
    std::function<std::optional<std::vector<Entry>>(const std::string &)> list;
    std::function<bool(const std::string &, const std::filesystem::path &, std::string &)> copy;
};

inline bool safe_document_name(const std::string &name) {
    return !name.empty() && name != "." && name != ".." &&
        name.find_first_of("/\\:") == std::string::npos && name.find('\0') == std::string::npos;
}

// Copies only the pack selected by the player, with its subfolders. The
// caller owns a fresh staging directory and leaves installed packs alone.
inline bool import_texture_documents(const TextureDocuments &documents, const std::string &uri,
    const std::filesystem::path &staging, std::string &error) {
    namespace fs = std::filesystem;
    std::set<std::string> visited;
    std::size_t files = 0;
    std::function<bool(const std::string &, const fs::path &, unsigned)> copy_tree;
    copy_tree = [&](const std::string &folder, const fs::path &target, unsigned depth) {
        if (depth > 32 || !visited.insert(folder).second) {
            error = "The selected folder contains a loop or too many nested folders.";
            return false;
        }
        const auto entries = documents.list(folder);
        if (!entries) {
            error = "Android could not read the selected texture folder.";
            return false;
        }
        std::error_code ec;
        if (!fs::create_directory(target, ec) || ec) {
            error = "Cannot create temporary texture storage; check free space.";
            return false;
        }
        std::set<std::string> names;
        for (const Entry &entry : *entries) {
            if (!safe_document_name(entry.name) || !names.insert(entry.name).second) {
                error = "The texture folder contains an invalid or duplicate filename.";
                return false;
            }
            // File manager metadata is never part of a texture pack.
            if (entry.name.front() == '.') continue;
            const fs::path child = target / fs::u8path(entry.name);
            if (entry.directory) {
                if (!copy_tree(entry.uri, child, depth + 1)) return false;
            } else {
                if (++files > 200000) {
                    error = "Too many files; select the extracted texture-pack folder itself.";
                    return false;
                }
                if (!documents.copy(entry.uri, child, error)) return false;
            }
        }
        return true;
    };
    // Require the actual pack folder so an accidental selection of an entire
    // memory stick does not copy unrelated personal files into app storage.
    const auto entries = documents.list(uri);
    if (!entries) {
        error = "Android could not read the selected folder. Choose it again and allow folder access.";
        return false;
    }
    bool has_ini = false;
    for (const Entry &entry : *entries)
        if (!entry.directory && entry.name == "textures.ini") has_ini = true;
    if (!has_ini) {
        error = "Choose the extracted pack folder containing textures.ini, not the ZIP or its parent folder.";
        return false;
    }
    return copy_tree(uri, staging, 0);
}

} // namespace mhp2g::android
