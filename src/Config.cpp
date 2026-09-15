#include "Config.h"
#include <fstream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::filesystem::path GetConfigPath() {
#ifdef _WIN32
    char buffer[4096] = {};
    unsigned long length = GetModuleFileNameA(nullptr, buffer, static_cast<unsigned long>(sizeof(buffer)));
    if (length > 0 && length < sizeof(buffer)) {
        return std::filesystem::path(buffer).parent_path() / "editor_config.txt";
    }
#else
    std::error_code ec;
    std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) {
        return executable.parent_path() / "editor_config.txt";
    }
#endif
    return std::filesystem::current_path() / "editor_config.txt";
}
}

void LoadConfig(EditorConfig& cfg) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open()) return;

    std::getline(file, cfg.gamePath);
    std::getline(file, cfg.lastMatFile);

    std::string line;
    if (std::getline(file, line)) try { cfg.shapeType = std::stoi(line); } catch(...) {}
    if (std::getline(file, line)) try { cfg.lightMode = std::stoi(line); } catch(...) {}
    if (std::getline(file, line)) cfg.useNormal = (line == "1");
    if (std::getline(file, line)) cfg.useGloss = (line == "1");
    if (std::getline(file, line)) cfg.useLuma = (line == "1");
    if (std::getline(file, line)) cfg.useBump = (line == "1");
}

void SaveConfig(const EditorConfig& cfg) {
    std::ofstream file(GetConfigPath());
    if (!file.is_open()) return;

    file << cfg.gamePath << "\n";
    file << cfg.lastMatFile << "\n";
    file << cfg.shapeType << "\n";
    file << cfg.lightMode << "\n";
    file << (cfg.useNormal ? "1" : "0") << "\n";
    file << (cfg.useGloss ? "1" : "0") << "\n";
    file << (cfg.useLuma ? "1" : "0") << "\n";
    file << (cfg.useBump ? "1" : "0") << "\n";
}
