#include "Config.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::filesystem::path GetConfigPath() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::filesystem::path(appdata) / "matedit" / "editor_config.txt";
    if (const char* up = std::getenv("USERPROFILE"); up && *up)
        return std::filesystem::path(up) / ".config" / "matedit" / "editor_config.txt";
    return std::filesystem::current_path() / "editor_config.txt";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "matedit" / "editor_config.txt";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "matedit" / "editor_config.txt";
    return std::filesystem::current_path() / "editor_config.txt";
#endif
}

std::filesystem::path GetConfigReadPath() {
    const auto primary = GetConfigPath();
    std::error_code ec;
    if (std::filesystem::exists(primary, ec)) return primary;
#ifdef _WIN32
    if (const char* up = std::getenv("USERPROFILE"); up && *up) {
        auto alt = std::filesystem::path(up) / ".config" / "matedit" / "editor_config.txt";
        if (std::filesystem::exists(alt, ec)) return alt;
    }
#endif
    return primary;
}

void ParseOptionalConfig(EditorConfig& cfg, const std::string& line) {
    if (line.rfind("LANGUAGE=", 0) == 0) {
        const std::string language = line.substr(9);
        if (language == "en" || language == "ru") cfg.language = language;
    }
    else if (line.rfind("SHOW_FPS=", 0) == 0) cfg.showFps = line.substr(9) != "0";
    else if (line.rfind("VSYNC=", 0) == 0) cfg.vsyncEnabled = line.substr(6) != "0";
    else if (line.rfind("TEXTURE_FILTERING=", 0) == 0) cfg.textureFilteringEnabled = line.substr(18) != "0";
    else if (line.rfind("AUTO_ASSIGN_MATERIAL_TEXTURES=", 0) == 0) cfg.autoAssignMaterialTextures = line.substr(30) != "0";
    else if (line.rfind("HIGH_LIGHT_INTENSITY=", 0) == 0) cfg.allowHighLightIntensity = line.substr(21) != "0";
    else if (line.rfind("FOV=", 0) == 0) { try { cfg.fov = std::clamp(std::stof(line.substr(4)), 60.0f, 120.0f); } catch (...) {} }
    else if (line.rfind("UNLIMITED_ZOOM=", 0) == 0) cfg.unlimitedZoom = line.substr(15) != "0";
    else if (line.rfind("FXAA=", 0) == 0) cfg.fxaaEnabled = line.substr(5) != "0";
    else if (line.rfind("TAA=", 0) == 0) cfg.taaEnabled = line.substr(4) != "0";
    else if (line.rfind("MSAA=", 0) == 0) cfg.msaaEnabled = line.substr(5) != "0";
    else if (line.rfind("MSAA_SAMPLES=", 0) == 0) { try { cfg.msaaSamples = std::clamp(std::stoi(line.substr(13)), 2, 8); } catch (...) {} }
    else if (line.rfind("KEY_TOGGLE_PANELS=", 0) == 0) { try { cfg.keyTogglePanels = std::stoi(line.substr(18)); } catch (...) {} }
    else if (line.rfind("KEY_FREECAM=", 0) == 0) { try { cfg.keyFreeCam = std::stoi(line.substr(12)); } catch (...) {} }
    else if (line.rfind("KEY_SETTINGS=", 0) == 0) { try { cfg.keyOpenSettings = std::stoi(line.substr(13)); } catch (...) {} }
    else if (line.rfind("KEY_FORWARD=", 0) == 0) { try { cfg.keyForward = std::stoi(line.substr(12)); } catch (...) {} }
    else if (line.rfind("KEY_BACKWARD=", 0) == 0) { try { cfg.keyBackward = std::stoi(line.substr(13)); } catch (...) {} }
    else if (line.rfind("KEY_LEFT=", 0) == 0) { try { cfg.keyLeft = std::stoi(line.substr(9)); } catch (...) {} }
    else if (line.rfind("KEY_RIGHT=", 0) == 0) { try { cfg.keyRight = std::stoi(line.substr(10)); } catch (...) {} }
    else if (line.rfind("KEY_UP=", 0) == 0) { try { cfg.keyUp = std::stoi(line.substr(7)); } catch (...) {} }
    else if (line.rfind("KEY_DOWN=", 0) == 0) { try { cfg.keyDown = std::stoi(line.substr(9)); } catch (...) {} }
    else if (line.rfind("KEY_SPRINT=", 0) == 0) { try { cfg.keySprint = std::stoi(line.substr(11)); } catch (...) {} }
    else if (line.rfind("KEY_TOGGLE_MODEL=", 0) == 0) { try { cfg.keyToggleModel = std::stoi(line.substr(17)); } catch (...) {} }
    else if (line.rfind("THEME=", 0) == 0) cfg.themeName = line.substr(6);
    else if (line.rfind("CUSTOM_THEME_NAME=", 0) == 0) cfg.customThemeName = line.substr(18);
    else if (line.rfind("HAS_CUSTOM_THEME=", 0) == 0) cfg.hasCustomTheme = line.substr(17) != "0";
    else if (line.rfind("CUSTOM_THEME_WINDOW_BG=", 0) == 0) std::sscanf(line.substr(23).c_str(), "%f %f %f %f", cfg.customThemeWindowBg, cfg.customThemeWindowBg + 1, cfg.customThemeWindowBg + 2, cfg.customThemeWindowBg + 3);
    else if (line.rfind("CUSTOM_THEME_CHILD_BG=", 0) == 0) std::sscanf(line.substr(22).c_str(), "%f %f %f %f", cfg.customThemeChildBg, cfg.customThemeChildBg + 1, cfg.customThemeChildBg + 2, cfg.customThemeChildBg + 3);
    else if (line.rfind("CUSTOM_THEME_POPUP_BG=", 0) == 0) std::sscanf(line.substr(22).c_str(), "%f %f %f %f", cfg.customThemePopupBg, cfg.customThemePopupBg + 1, cfg.customThemePopupBg + 2, cfg.customThemePopupBg + 3);
    else if (line.rfind("CUSTOM_THEME_FRAME_BG=", 0) == 0) std::sscanf(line.substr(22).c_str(), "%f %f %f %f", cfg.customThemeFrameBg, cfg.customThemeFrameBg + 1, cfg.customThemeFrameBg + 2, cfg.customThemeFrameBg + 3);
    else if (line.rfind("CUSTOM_THEME_FRAME_HOVERED=", 0) == 0) std::sscanf(line.substr(27).c_str(), "%f %f %f %f", cfg.customThemeFrameHovered, cfg.customThemeFrameHovered + 1, cfg.customThemeFrameHovered + 2, cfg.customThemeFrameHovered + 3);
    else if (line.rfind("CUSTOM_THEME_FRAME_ACTIVE=", 0) == 0) std::sscanf(line.substr(26).c_str(), "%f %f %f %f", cfg.customThemeFrameActive, cfg.customThemeFrameActive + 1, cfg.customThemeFrameActive + 2, cfg.customThemeFrameActive + 3);
    else if (line.rfind("CUSTOM_THEME_BUTTON=", 0) == 0) std::sscanf(line.substr(20).c_str(), "%f %f %f %f", cfg.customThemeButton, cfg.customThemeButton + 1, cfg.customThemeButton + 2, cfg.customThemeButton + 3);
    else if (line.rfind("CUSTOM_THEME_TEXT=", 0) == 0) std::sscanf(line.substr(18).c_str(), "%f %f %f %f", cfg.customThemeText, cfg.customThemeText + 1, cfg.customThemeText + 2, cfg.customThemeText + 3);
    else if (line.rfind("CUSTOM_THEME_TEXT_DISABLED=", 0) == 0) std::sscanf(line.substr(27).c_str(), "%f %f %f %f", cfg.customThemeTextDisabled, cfg.customThemeTextDisabled + 1, cfg.customThemeTextDisabled + 2, cfg.customThemeTextDisabled + 3);
    else if (line.rfind("CUSTOM_THEME_ACCENT=", 0) == 0) std::sscanf(line.substr(20).c_str(), "%f %f %f %f", cfg.customThemeAccent, cfg.customThemeAccent + 1, cfg.customThemeAccent + 2, cfg.customThemeAccent + 3);
    else if (line.rfind("CUSTOM_THEME_BORDER=", 0) == 0) std::sscanf(line.substr(20).c_str(), "%f %f %f %f", cfg.customThemeBorder, cfg.customThemeBorder + 1, cfg.customThemeBorder + 2, cfg.customThemeBorder + 3);
    else if (line.rfind("CUSTOM_THEME_TITLE_BG=", 0) == 0) std::sscanf(line.substr(22).c_str(), "%f %f %f %f", cfg.customThemeTitleBg, cfg.customThemeTitleBg + 1, cfg.customThemeTitleBg + 2, cfg.customThemeTitleBg + 3);
    else if (line.rfind("CUSTOM_THEME_TITLE_BG_ACTIVE=", 0) == 0) std::sscanf(line.substr(29).c_str(), "%f %f %f %f", cfg.customThemeTitleBgActive, cfg.customThemeTitleBgActive + 1, cfg.customThemeTitleBgActive + 2, cfg.customThemeTitleBgActive + 3);
    else if (line.rfind("CUSTOM_THEME_MENU_BAR_BG=", 0) == 0) std::sscanf(line.substr(25).c_str(), "%f %f %f %f", cfg.customThemeMenuBarBg, cfg.customThemeMenuBarBg + 1, cfg.customThemeMenuBarBg + 2, cfg.customThemeMenuBarBg + 3);
    else if (line.rfind("CUSTOM_THEME_SCROLLBAR_BG=", 0) == 0) std::sscanf(line.substr(26).c_str(), "%f %f %f %f", cfg.customThemeScrollbarBg, cfg.customThemeScrollbarBg + 1, cfg.customThemeScrollbarBg + 2, cfg.customThemeScrollbarBg + 3);
    else if (line.rfind("CUSTOM_THEME_SCROLLBAR_GRAB=", 0) == 0) std::sscanf(line.substr(28).c_str(), "%f %f %f %f", cfg.customThemeScrollbarGrab, cfg.customThemeScrollbarGrab + 1, cfg.customThemeScrollbarGrab + 2, cfg.customThemeScrollbarGrab + 3);
    else if (line.rfind("CUSTOM_THEME_HEADER=", 0) == 0) std::sscanf(line.substr(20).c_str(), "%f %f %f %f", cfg.customThemeHeader, cfg.customThemeHeader + 1, cfg.customThemeHeader + 2, cfg.customThemeHeader + 3);
    else if (line.rfind("CUSTOM_THEME_HEADER_HOVERED=", 0) == 0) std::sscanf(line.substr(28).c_str(), "%f %f %f %f", cfg.customThemeHeaderHovered, cfg.customThemeHeaderHovered + 1, cfg.customThemeHeaderHovered + 2, cfg.customThemeHeaderHovered + 3);
    else if (line.rfind("CUSTOM_THEME_HEADER_ACTIVE=", 0) == 0) std::sscanf(line.substr(27).c_str(), "%f %f %f %f", cfg.customThemeHeaderActive, cfg.customThemeHeaderActive + 1, cfg.customThemeHeaderActive + 2, cfg.customThemeHeaderActive + 3);
    else if (line.rfind("CUSTOM_THEME_SEPARATOR=", 0) == 0) std::sscanf(line.substr(23).c_str(), "%f %f %f %f", cfg.customThemeSeparator, cfg.customThemeSeparator + 1, cfg.customThemeSeparator + 2, cfg.customThemeSeparator + 3);
    else if (line.rfind("CUSTOM_THEME_RESIZE_GRIP=", 0) == 0) std::sscanf(line.substr(25).c_str(), "%f %f %f %f", cfg.customThemeResizeGrip, cfg.customThemeResizeGrip + 1, cfg.customThemeResizeGrip + 2, cfg.customThemeResizeGrip + 3);
    else if (line.rfind("CUSTOM_THEME_TAB=", 0) == 0) std::sscanf(line.substr(17).c_str(), "%f %f %f %f", cfg.customThemeTab, cfg.customThemeTab + 1, cfg.customThemeTab + 2, cfg.customThemeTab + 3);
    else if (line.rfind("CUSTOM_THEME_TAB_HOVERED=", 0) == 0) std::sscanf(line.substr(25).c_str(), "%f %f %f %f", cfg.customThemeTabHovered, cfg.customThemeTabHovered + 1, cfg.customThemeTabHovered + 2, cfg.customThemeTabHovered + 3);
    else if (line.rfind("CUSTOM_THEME_TAB_ACTIVE=", 0) == 0) std::sscanf(line.substr(24).c_str(), "%f %f %f %f", cfg.customThemeTabActive, cfg.customThemeTabActive + 1, cfg.customThemeTabActive + 2, cfg.customThemeTabActive + 3);
    else if (line.rfind("CUSTOM_THEME_TEXT_SELECTED=", 0) == 0) std::sscanf(line.substr(27).c_str(), "%f %f %f %f", cfg.customThemeTextSelected, cfg.customThemeTextSelected + 1, cfg.customThemeTextSelected + 2, cfg.customThemeTextSelected + 3);
    else if (line.rfind("CUSTOM_THEME_DRAG_DROP=", 0) == 0) std::sscanf(line.substr(23).c_str(), "%f %f %f %f", cfg.customThemeDragDrop, cfg.customThemeDragDrop + 1, cfg.customThemeDragDrop + 2, cfg.customThemeDragDrop + 3);
    else if (line.rfind("CUSTOM_THEME_NAV=", 0) == 0) std::sscanf(line.substr(17).c_str(), "%f %f %f %f", cfg.customThemeNav, cfg.customThemeNav + 1, cfg.customThemeNav + 2, cfg.customThemeNav + 3);
    else if (line.rfind("CUSTOM_THEME_TABLE_HEADER=", 0) == 0) std::sscanf(line.substr(26).c_str(), "%f %f %f %f", cfg.customThemeTableHeader, cfg.customThemeTableHeader + 1, cfg.customThemeTableHeader + 2, cfg.customThemeTableHeader + 3);
    else if (line.rfind("CUSTOM_THEME_TABLE_BORDER=", 0) == 0) std::sscanf(line.substr(26).c_str(), "%f %f %f %f", cfg.customThemeTableBorder, cfg.customThemeTableBorder + 1, cfg.customThemeTableBorder + 2, cfg.customThemeTableBorder + 3);
    else if (line.rfind("CUSTOM_THEME_TABLE_ROW=", 0) == 0) std::sscanf(line.substr(23).c_str(), "%f %f %f %f", cfg.customThemeTableRow, cfg.customThemeTableRow + 1, cfg.customThemeTableRow + 2, cfg.customThemeTableRow + 3);
    else if (line.rfind("CUSTOM_THEME_TABLE_ROW_ALT=", 0) == 0) std::sscanf(line.substr(27).c_str(), "%f %f %f %f", cfg.customThemeTableRowAlt, cfg.customThemeTableRowAlt + 1, cfg.customThemeTableRowAlt + 2, cfg.customThemeTableRowAlt + 3);
    else if (line.rfind("CUSTOM_THEME_PLOT=", 0) == 0) std::sscanf(line.substr(18).c_str(), "%f %f %f %f", cfg.customThemePlot, cfg.customThemePlot + 1, cfg.customThemePlot + 2, cfg.customThemePlot + 3);
    else if (line.rfind("CUSTOM_THEME_PLOT_HOVERED=", 0) == 0) std::sscanf(line.substr(26).c_str(), "%f %f %f %f", cfg.customThemePlotHovered, cfg.customThemePlotHovered + 1, cfg.customThemePlotHovered + 2, cfg.customThemePlotHovered + 3);
    else if (line.rfind("CUSTOM_THEME_VIEWPORT_BG=", 0) == 0) std::sscanf(line.substr(25).c_str(), "%f %f %f", cfg.customThemeViewportBg, cfg.customThemeViewportBg + 1, cfg.customThemeViewportBg + 2);
}
}

std::filesystem::path GetConfigDirectory() {
    return GetConfigPath().parent_path();
}

void LoadConfig(EditorConfig& cfg) {
    std::ifstream file(GetConfigReadPath());
    if (!file.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
    if (lines.size() < 8) return;

    cfg.gamePath = lines[0];
    cfg.lastMatFile = lines[1];
    try { cfg.shapeType = std::stoi(lines[2]); } catch (...) {}
    try { cfg.lightMode = std::stoi(lines[3]); } catch (...) {}

    float probeR = 0.0f, probeG = 0.0f, probeB = 0.0f;
    const bool newFormat = lines.size() >= 15 && std::sscanf(lines[8].c_str(), "%f %f %f", &probeR, &probeG, &probeB) == 3;
    if (newFormat) {
        try { cfg.dynamicLightSpeed = std::stof(lines[4]); } catch (...) {}
        try { cfg.dynamicLightRadius = std::stof(lines[5]); } catch (...) {}
        cfg.autoLoadWads = lines[6] != "0";
        try { cfg.backgroundMode = std::stoi(lines[7]); } catch (...) {}
        std::sscanf(lines[8].c_str(), "%f %f %f", &cfg.backgroundColor[0], &cfg.backgroundColor[1], &cfg.backgroundColor[2]);
        cfg.useNormal = lines[10] == "1";
        cfg.useGloss = lines[11] == "1";
        cfg.useLuma = lines[12] == "1";
        cfg.useBump = lines[13] == "1";
        cfg.loadedWads.clear();
        if (lines.size() > 14) {
            try {
                const int count = std::max(0, std::stoi(lines[14]));
                for (int i = 0; i < count && 15 + i < static_cast<int>(lines.size()); ++i) {
                    if (!lines[15 + i].empty()) cfg.loadedWads.push_back(lines[15 + i]);
                }
                const int skyboxLine = 15 + count;
                if (skyboxLine < static_cast<int>(lines.size()) && lines[skyboxLine].rfind("SKYBOX=", 0) == 0) cfg.skyboxName = lines[skyboxLine].substr(7);
            } catch (...) {}
        }
    } else {
        cfg.useNormal = lines[4] == "1";
        cfg.useGloss = lines[5] == "1";
        cfg.useLuma = lines[6] == "1";
        cfg.useBump = lines[7] == "1";
        cfg.loadedWads.clear();
        if (lines.size() > 8) {
            try {
                const int count = std::max(0, std::stoi(lines[8]));
                for (int i = 0; i < count && 9 + i < static_cast<int>(lines.size()); ++i) {
                    if (!lines[9 + i].empty()) cfg.loadedWads.push_back(lines[9 + i]);
                }
            } catch (...) {}
        }
    }

    for (const std::string& configLine : lines) ParseOptionalConfig(cfg, configLine);
    if (!cfg.themeName.empty() && cfg.themeName != "ImGui" && cfg.themeName != "Pastel Pink" && cfg.themeName != "Pastel Green" && cfg.themeName != "AMOLED") cfg.hasCustomTheme = true;
}

void SaveConfig(const EditorConfig& cfg) {
    const std::filesystem::path path = GetConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path);
    if (!file.is_open()) return;

    file << cfg.gamePath << "\n";
    file << cfg.lastMatFile << "\n";
    file << cfg.shapeType << "\n";
    file << cfg.lightMode << "\n";
    file << cfg.dynamicLightSpeed << "\n";
    file << cfg.dynamicLightRadius << "\n";
    file << (cfg.autoLoadWads ? "1" : "0") << "\n";
    file << cfg.backgroundMode << "\n";
    file << cfg.backgroundColor[0] << " " << cfg.backgroundColor[1] << " " << cfg.backgroundColor[2] << "\n";
    file << "\n";
    file << (cfg.useNormal ? "1" : "0") << "\n";
    file << (cfg.useGloss ? "1" : "0") << "\n";
    file << (cfg.useLuma ? "1" : "0") << "\n";
    file << (cfg.useBump ? "1" : "0") << "\n";
    file << cfg.loadedWads.size() << "\n";
    for (const auto& wad : cfg.loadedWads) file << wad << "\n";
    file << "SKYBOX=" << cfg.skyboxName << "\n";
    file << "SHOW_FPS=" << (cfg.showFps ? "1" : "0") << "\n";
    file << "VSYNC=" << (cfg.vsyncEnabled ? "1" : "0") << "\n";
    file << "TEXTURE_FILTERING=" << (cfg.textureFilteringEnabled ? "1" : "0") << "\n";
    file << "AUTO_ASSIGN_MATERIAL_TEXTURES=" << (cfg.autoAssignMaterialTextures ? "1" : "0") << "\n";
    file << "LANGUAGE=" << (cfg.language == "ru" ? "ru" : "en") << "\n";
    file << "HIGH_LIGHT_INTENSITY=" << (cfg.allowHighLightIntensity ? "1" : "0") << "\n";
    file << "FOV=" << cfg.fov << "\n";
    file << "UNLIMITED_ZOOM=" << (cfg.unlimitedZoom ? "1" : "0") << "\n";
    file << "FXAA=" << (cfg.fxaaEnabled ? "1" : "0") << "\n";
    file << "TAA=" << (cfg.taaEnabled ? "1" : "0") << "\n";
    file << "MSAA=" << (cfg.msaaEnabled ? "1" : "0") << "\n";
    file << "MSAA_SAMPLES=" << cfg.msaaSamples << "\n";
    file << "KEY_TOGGLE_PANELS=" << cfg.keyTogglePanels << "\n";
    file << "KEY_FREECAM=" << cfg.keyFreeCam << "\n";
    file << "KEY_SETTINGS=" << cfg.keyOpenSettings << "\n";
    file << "KEY_FORWARD=" << cfg.keyForward << "\n";
    file << "KEY_BACKWARD=" << cfg.keyBackward << "\n";
    file << "KEY_LEFT=" << cfg.keyLeft << "\n";
    file << "KEY_RIGHT=" << cfg.keyRight << "\n";
    file << "KEY_UP=" << cfg.keyUp << "\n";
    file << "KEY_DOWN=" << cfg.keyDown << "\n";
    file << "KEY_SPRINT=" << cfg.keySprint << "\n";
    file << "KEY_TOGGLE_MODEL=" << cfg.keyToggleModel << "\n";
    file << "THEME=" << cfg.themeName << "\n";
    file << "CUSTOM_THEME_NAME=" << cfg.customThemeName << "\n";
    file << "HAS_CUSTOM_THEME=" << (cfg.hasCustomTheme ? "1" : "0") << "\n";
    file << "CUSTOM_THEME_WINDOW_BG=" << cfg.customThemeWindowBg[0] << " " << cfg.customThemeWindowBg[1] << " " << cfg.customThemeWindowBg[2] << " " << cfg.customThemeWindowBg[3] << "\n";
    file << "CUSTOM_THEME_CHILD_BG=" << cfg.customThemeChildBg[0] << " " << cfg.customThemeChildBg[1] << " " << cfg.customThemeChildBg[2] << " " << cfg.customThemeChildBg[3] << "\n";
    file << "CUSTOM_THEME_POPUP_BG=" << cfg.customThemePopupBg[0] << " " << cfg.customThemePopupBg[1] << " " << cfg.customThemePopupBg[2] << " " << cfg.customThemePopupBg[3] << "\n";
    file << "CUSTOM_THEME_FRAME_BG=" << cfg.customThemeFrameBg[0] << " " << cfg.customThemeFrameBg[1] << " " << cfg.customThemeFrameBg[2] << " " << cfg.customThemeFrameBg[3] << "\n";
    file << "CUSTOM_THEME_FRAME_HOVERED=" << cfg.customThemeFrameHovered[0] << " " << cfg.customThemeFrameHovered[1] << " " << cfg.customThemeFrameHovered[2] << " " << cfg.customThemeFrameHovered[3] << "\n";
    file << "CUSTOM_THEME_FRAME_ACTIVE=" << cfg.customThemeFrameActive[0] << " " << cfg.customThemeFrameActive[1] << " " << cfg.customThemeFrameActive[2] << " " << cfg.customThemeFrameActive[3] << "\n";
    file << "CUSTOM_THEME_BUTTON=" << cfg.customThemeButton[0] << " " << cfg.customThemeButton[1] << " " << cfg.customThemeButton[2] << " " << cfg.customThemeButton[3] << "\n";
    file << "CUSTOM_THEME_TEXT=" << cfg.customThemeText[0] << " " << cfg.customThemeText[1] << " " << cfg.customThemeText[2] << " " << cfg.customThemeText[3] << "\n";
    file << "CUSTOM_THEME_TEXT_DISABLED=" << cfg.customThemeTextDisabled[0] << " " << cfg.customThemeTextDisabled[1] << " " << cfg.customThemeTextDisabled[2] << " " << cfg.customThemeTextDisabled[3] << "\n";
    file << "CUSTOM_THEME_ACCENT=" << cfg.customThemeAccent[0] << " " << cfg.customThemeAccent[1] << " " << cfg.customThemeAccent[2] << " " << cfg.customThemeAccent[3] << "\n";
    file << "CUSTOM_THEME_BORDER=" << cfg.customThemeBorder[0] << " " << cfg.customThemeBorder[1] << " " << cfg.customThemeBorder[2] << " " << cfg.customThemeBorder[3] << "\n";
    file << "CUSTOM_THEME_TITLE_BG=" << cfg.customThemeTitleBg[0] << " " << cfg.customThemeTitleBg[1] << " " << cfg.customThemeTitleBg[2] << " " << cfg.customThemeTitleBg[3] << "\n";
    file << "CUSTOM_THEME_TITLE_BG_ACTIVE=" << cfg.customThemeTitleBgActive[0] << " " << cfg.customThemeTitleBgActive[1] << " " << cfg.customThemeTitleBgActive[2] << " " << cfg.customThemeTitleBgActive[3] << "\n";
    file << "CUSTOM_THEME_MENU_BAR_BG=" << cfg.customThemeMenuBarBg[0] << " " << cfg.customThemeMenuBarBg[1] << " " << cfg.customThemeMenuBarBg[2] << " " << cfg.customThemeMenuBarBg[3] << "\n";
    file << "CUSTOM_THEME_SCROLLBAR_BG=" << cfg.customThemeScrollbarBg[0] << " " << cfg.customThemeScrollbarBg[1] << " " << cfg.customThemeScrollbarBg[2] << " " << cfg.customThemeScrollbarBg[3] << "\n";
    file << "CUSTOM_THEME_SCROLLBAR_GRAB=" << cfg.customThemeScrollbarGrab[0] << " " << cfg.customThemeScrollbarGrab[1] << " " << cfg.customThemeScrollbarGrab[2] << " " << cfg.customThemeScrollbarGrab[3] << "\n";
    file << "CUSTOM_THEME_HEADER=" << cfg.customThemeHeader[0] << " " << cfg.customThemeHeader[1] << " " << cfg.customThemeHeader[2] << " " << cfg.customThemeHeader[3] << "\n";
    file << "CUSTOM_THEME_HEADER_HOVERED=" << cfg.customThemeHeaderHovered[0] << " " << cfg.customThemeHeaderHovered[1] << " " << cfg.customThemeHeaderHovered[2] << " " << cfg.customThemeHeaderHovered[3] << "\n";
    file << "CUSTOM_THEME_HEADER_ACTIVE=" << cfg.customThemeHeaderActive[0] << " " << cfg.customThemeHeaderActive[1] << " " << cfg.customThemeHeaderActive[2] << " " << cfg.customThemeHeaderActive[3] << "\n";
    file << "CUSTOM_THEME_SEPARATOR=" << cfg.customThemeSeparator[0] << " " << cfg.customThemeSeparator[1] << " " << cfg.customThemeSeparator[2] << " " << cfg.customThemeSeparator[3] << "\n";
    file << "CUSTOM_THEME_RESIZE_GRIP=" << cfg.customThemeResizeGrip[0] << " " << cfg.customThemeResizeGrip[1] << " " << cfg.customThemeResizeGrip[2] << " " << cfg.customThemeResizeGrip[3] << "\n";
    file << "CUSTOM_THEME_TAB=" << cfg.customThemeTab[0] << " " << cfg.customThemeTab[1] << " " << cfg.customThemeTab[2] << " " << cfg.customThemeTab[3] << "\n";
    file << "CUSTOM_THEME_TAB_HOVERED=" << cfg.customThemeTabHovered[0] << " " << cfg.customThemeTabHovered[1] << " " << cfg.customThemeTabHovered[2] << " " << cfg.customThemeTabHovered[3] << "\n";
    file << "CUSTOM_THEME_TAB_ACTIVE=" << cfg.customThemeTabActive[0] << " " << cfg.customThemeTabActive[1] << " " << cfg.customThemeTabActive[2] << " " << cfg.customThemeTabActive[3] << "\n";
    file << "CUSTOM_THEME_TEXT_SELECTED=" << cfg.customThemeTextSelected[0] << " " << cfg.customThemeTextSelected[1] << " " << cfg.customThemeTextSelected[2] << " " << cfg.customThemeTextSelected[3] << "\n";
    file << "CUSTOM_THEME_DRAG_DROP=" << cfg.customThemeDragDrop[0] << " " << cfg.customThemeDragDrop[1] << " " << cfg.customThemeDragDrop[2] << " " << cfg.customThemeDragDrop[3] << "\n";
    file << "CUSTOM_THEME_NAV=" << cfg.customThemeNav[0] << " " << cfg.customThemeNav[1] << " " << cfg.customThemeNav[2] << " " << cfg.customThemeNav[3] << "\n";
    file << "CUSTOM_THEME_TABLE_HEADER=" << cfg.customThemeTableHeader[0] << " " << cfg.customThemeTableHeader[1] << " " << cfg.customThemeTableHeader[2] << " " << cfg.customThemeTableHeader[3] << "\n";
    file << "CUSTOM_THEME_TABLE_BORDER=" << cfg.customThemeTableBorder[0] << " " << cfg.customThemeTableBorder[1] << " " << cfg.customThemeTableBorder[2] << " " << cfg.customThemeTableBorder[3] << "\n";
    file << "CUSTOM_THEME_TABLE_ROW=" << cfg.customThemeTableRow[0] << " " << cfg.customThemeTableRow[1] << " " << cfg.customThemeTableRow[2] << " " << cfg.customThemeTableRow[3] << "\n";
    file << "CUSTOM_THEME_TABLE_ROW_ALT=" << cfg.customThemeTableRowAlt[0] << " " << cfg.customThemeTableRowAlt[1] << " " << cfg.customThemeTableRowAlt[2] << " " << cfg.customThemeTableRowAlt[3] << "\n";
    file << "CUSTOM_THEME_PLOT=" << cfg.customThemePlot[0] << " " << cfg.customThemePlot[1] << " " << cfg.customThemePlot[2] << " " << cfg.customThemePlot[3] << "\n";
    file << "CUSTOM_THEME_PLOT_HOVERED=" << cfg.customThemePlotHovered[0] << " " << cfg.customThemePlotHovered[1] << " " << cfg.customThemePlotHovered[2] << " " << cfg.customThemePlotHovered[3] << "\n";
    file << "CUSTOM_THEME_VIEWPORT_BG=" << cfg.customThemeViewportBg[0] << " " << cfg.customThemeViewportBg[1] << " " << cfg.customThemeViewportBg[2] << "\n";
}