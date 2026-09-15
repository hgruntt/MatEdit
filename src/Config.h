#pragma once
#include <string>
#define _CRT_SECURE_NO_WARNINGS

struct EditorConfig {
    std::string gamePath = "";
    std::string lastMatFile = "";
    int shapeType = 0;
    int lightMode = 0;
    bool useNormal = true;
    bool useGloss = true;
    bool useLuma = true;
    bool useBump = true;
};

void LoadConfig(EditorConfig& cfg);
void SaveConfig(const EditorConfig& cfg);
