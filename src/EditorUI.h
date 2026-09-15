#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include "MaterialSystem.h"
#include "Config.h"
#include <vector>
#include <functional>
void SetupModernDarkStyle();

void DrawEditorUI(
    int display_w, int display_h,
    EditorConfig& editorCfg,
    std::vector<Material>& materials,
    std::vector<PhysicalMaterialEntry>& physicalMaterials,
    std::vector<std::string>& matFiles,
    std::string& currentFileName,
    int& currentMatIndex,
    std::string& currentDefFile,
    int& currentPhysMatIndex,
    int& shapeType,
    int& lightMode,
    bool& useNormal,
    bool& useGloss,
    bool& useLuma,
    bool& useBump,
    float& lightIntensity,
    float* lightColor,
    GLuint& skyboxID,
    std::function<void(std::string&, int&)> refreshDataFunc
);
