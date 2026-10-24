#pragma once
#include "save_data/cloud_save.hpp"

namespace mhp2g::cloud {
void configure(const std::filesystem::path &stick, const std::filesystem::path &data, const std::string &endpoint);
bool configured();
bool busy();
void begin(bool allow_download, Choice choice = Choice::Automatic, const std::string &expected = {});
std::optional<Result> poll();
std::string status();
std::filesystem::path backups();
void save_committed();
void pause_session();
void finish();
}

namespace mhp2g::ui {
bool run_cloud_startup();
void cloud_rows();
bool cloud_screen(bool back);
}
