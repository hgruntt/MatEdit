#include "Config.h"
#include <fstream>
#define _CRT_SECURE_NO_WARNINGS

void LoadConfig(EditorConfig& cfg) {
    std::ifstream file("editor_config.txt");
    if (!file.is_open()) return;

    std::getline(file, cfg.gamePath);
    std::getline(file, cfg.lastMatFile);
    
    std::string line;
    if (std::getline(file, line)) try { cfg.shapeType = std::stoi(line); } catch(...) {}
    if (std::getline(file, line)) try { cfg.lightMode = std::stoi(line); } catch(...) {}
    if (std::getline(file, line)) cfg.useNormal = (line == "1");
    if (std::getline(file, line)) cfg.useGloss = (line == "1");
    if (std::getline(file, line)) cfg.useLuma = (line == "1");
}

void SaveConfig(const EditorConfig& cfg) {
    std::ofstream file("editor_config.txt");
    if (!file.is_open()) return;

    file << cfg.gamePath << "\n";
    file << cfg.lastMatFile << "\n";
    file << cfg.shapeType << "\n";
    file << cfg.lightMode << "\n";
    file << (cfg.useNormal ? "1" : "0") << "\n";
    file << (cfg.useGloss ? "1" : "0") << "\n";
    file << (cfg.useLuma ? "1" : "0") << "\n";
}
