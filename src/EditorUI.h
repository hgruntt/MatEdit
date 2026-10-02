#pragma once
#include "MaterialSystem.h"
#include "Config.h"
#include <vector>
#include <functional>
struct GLFWwindow;

void InitUI();
void SetEditorInputWindow(GLFWwindow* window);
void OpenEditorSettings();
void ReleaseEditorUIPreview();
bool IsEditorViewportHovered();
bool IsMaterialCreatorOpen();
void GetEditorViewportRect(float& x, float& y, float& w, float& h);
void DrawEditorViewportTexture(GLuint texture);
void DrawEditorViewportFPS(float fps);
void ToggleEditorPanels();
void DrawEditorUI(
    int display_w, int display_h,
    EditorConfig& editorCfg,
    std::vector<Material>& materials,
    std::vector<PhysicalMaterialEntry>& physicalMaterials,
    std::vector<std::string>& matFiles,
    std::vector<std::string>& defFiles,
    std::vector<std::string>& ddsFiles,
    std::string& currentFileName,
    int& currentMatIndex,
    std::string& currentDefFile,
    int& currentPhysMatIndex,
    int& shapeType,
    bool& modelVisible,
    int& lightMode,
    bool& useNormal,
    bool& useGloss,
    bool& useLuma,
    bool& useBump,
    float& lightIntensity,
    float& glossIntensity,
    float& normalIntensity,
    float& lumaIntensity,
    float& bumpIntensity,
    float& detailIntensity,
    float* lightColor,
    GLuint& skyboxTexture,
    std::function<void(std::string&, int&)> refreshDataFunc
);
