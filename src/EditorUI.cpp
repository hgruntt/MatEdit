#include "EditorUI.h"
#include "Skybox.h"
#include "Contributors.h"
#include "I18n.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <unordered_map>
#include <optional>
#include <numeric>
#include <chrono>
#include <cstdint>
#include <cmath>

namespace fs = std::filesystem;

namespace {

std::optional<fs::file_time_type> g_matTextMtime;
std::optional<fs::file_time_type> g_defTextMtime;
bool g_matTextReloadRequested = false;

bool SaveMaterialsAndRefreshText(const std::string& path, const std::vector<Material>& materials) {
    const bool saved = SaveAllMaterials(path, materials);
    if (saved) g_matTextReloadRequested = true;
    return saved;
}

std::string LoadTextFile(const std::string& relativePath) {
    if (relativePath.empty() || relativePath == "None") return {};
    const fs::path path = fs::path(relativePath).is_absolute() ? fs::path(relativePath) : (gameRootPath / relativePath);
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool SaveTextFile(const std::string& relativePath, const std::string& text) {
    if (relativePath.empty() || relativePath == "None") return false;
    const fs::path path = fs::path(relativePath).is_absolute() ? fs::path(relativePath) : (gameRootPath / relativePath);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

void SetTextBuffer(std::vector<char>& buffer, const std::string& text) {
    buffer.resize(text.size() + 1u);
    if (!text.empty()) std::memcpy(buffer.data(), text.data(), text.size());
    buffer[text.size()] = '\0';
}

bool MatchesFilter(const std::string& value, const std::string& filter);
std::string ToLower(std::string value);
bool DrawDeleteXButton(const char* id, float textY);

bool BeginSpacedModal(const char* name, ImGuiWindowFlags flags) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    if (!ImGui::BeginPopupModal(name, nullptr, flags)) {
        ImGui::PopStyleVar(2);
        return false;
    }
    return true;
}

void EndSpacedModal() {
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

struct DocumentFindState {
    char query[128] = {};
    std::string path;
    std::vector<char>* buffer = nullptr;
    int currentMatch = -1;
    int scrollToLine = -1;
    float scrollY = 0.0f;
    ImGuiWindow* documentWindow = nullptr;
    bool open = false;
    bool focusSearch = false;
};

int DocumentFindInputCallback(ImGuiInputTextCallbackData* data) {
    auto* state = static_cast<DocumentFindState*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* buffer = static_cast<std::vector<char>*>(state->buffer);
        buffer->resize(static_cast<size_t>(data->BufTextLen) + 1u);
        data->Buf = buffer->data();
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        state->documentWindow = ImGui::GetCurrentWindow();
        state->scrollY = state->documentWindow->Scroll.y;
    }
    return 0;
}

bool DrawFindArrowButton(const char* id, bool up) {
    const ImVec2 size(24.0f, 24.0f);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_HeaderActive : ImGuiCol_TextDisabled);
    const ImWchar glyphCode = up ? 0x2191 : 0x2193;
    ImFontBaked* font = ImGui::GetFontBaked();
    if (font && font->FindGlyphNoFallback(glyphCode)) {
        const char* glyph = up ? "\xe2\x86\x91" : "\xe2\x86\x93";
        const ImVec2 textSize = ImGui::CalcTextSize(glyph);
        drawList->AddText(ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f), color, glyph);
    } else {
        const float centerX = min.x + size.x * 0.5f;
        const float centerY = min.y + size.y * 0.5f;
        const float direction = up ? -1.0f : 1.0f;
        const ImVec2 tip(centerX, centerY + direction * 5.0f);
        const ImVec2 shaftEnd(centerX, centerY - direction * 4.0f);
        drawList->AddLine(tip, shaftEnd, color, 1.8f);
        drawList->AddLine(shaftEnd, ImVec2(centerX - 4.0f, centerY - direction * 0.5f), color, 1.8f);
        drawList->AddLine(shaftEnd, ImVec2(centerX + 4.0f, centerY - direction * 0.5f), color, 1.8f);
    }
    return pressed;
}

void DrawTextDocumentEditor(
    const char* id,
    const char* label,
    std::string& path,
    std::vector<char>& buffer,
    std::string& loadedPath,
    const std::vector<std::string>& files,
    const std::function<void(const std::string&)>& selectFile,
    const std::function<void()>& textChanged = {}) {
    ImGui::PushID(id);
    const char* popupId = "change_popup";

    if (path.empty() || path == "None") {
        ImGui::TextDisabled(Tr("NO %s FILE SELECTED"), label);
        if (!files.empty()) {
            if (ImGui::Button(Tr("CHANGE##change"))) ImGui::OpenPopup(popupId);
            if (ImGui::BeginPopup(popupId)) {
                for (const std::string& file : files) {
                    if (ImGui::Selectable(file.c_str())) {
                        path = file;
                        loadedPath.clear();
                        if (selectFile) selectFile(file);
                    }
                }
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
        return;
    }

    const fs::path absolutePath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::error_code mtimeEc;
    const auto currentMtime = fs::exists(absolutePath, mtimeEc) ? fs::last_write_time(absolutePath, mtimeEc) : fs::file_time_type{};
    std::optional<fs::file_time_type>& trackedMtime = std::strcmp(id, "##MatTextEditor") == 0 ? g_matTextMtime : g_defTextMtime;
    const bool forceReload = std::strcmp(id, "##MatTextEditor") == 0 && g_matTextReloadRequested;
    if (loadedPath != path || !trackedMtime || currentMtime != *trackedMtime || forceReload) {
        SetTextBuffer(buffer, LoadTextFile(path));
        loadedPath = path;
        trackedMtime = currentMtime;
        if (std::strcmp(id, "##MatTextEditor") == 0) g_matTextReloadRequested = false;
    }

    static DocumentFindState findState;
    findState.buffer = &buffer;
    if (findState.path != path) {
        findState.path = path;
        findState.query[0] = '\0';
        findState.currentMatch = -1;
        findState.scrollToLine = -1;
        findState.scrollY = 0.0f;
        findState.documentWindow = nullptr;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
        findState.open = true;
        findState.focusSearch = true;
    }

    ImGui::Text(Tr("%s"), path.c_str());
    ImGui::SameLine();
    if (ImGui::Button(Tr("CHANGE##change"))) ImGui::OpenPopup(popupId);
    if (ImGui::BeginPopup(popupId)) {
        for (const std::string& file : files) {
            if (ImGui::Selectable(file.c_str(), path == file)) {
                path = file;
                loadedPath.clear();
                if (selectFile) selectFile(file);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("RELOAD##reload"))) {
        SetTextBuffer(buffer, LoadTextFile(path));
        loadedPath = path;
    }

    if (buffer.empty()) SetTextBuffer(buffer, {});
    const float documentHeight = ImGui::GetContentRegionAvail().y;
    if (findState.scrollToLine >= 0) {
        const float lineHeight = ImGui::GetFontSize();
        const float lineCount = static_cast<float>(std::count(buffer.begin(), buffer.end(), '\n') + 1);
        const float maxScroll = std::max(0.0f, lineCount * lineHeight - documentHeight);
        const float centeredScroll = static_cast<float>(findState.scrollToLine) * lineHeight - documentHeight * 0.5f;
        findState.scrollY = std::clamp(centeredScroll, 0.0f, maxScroll);
        ImGui::SetNextWindowScroll(ImVec2(-1.0f, findState.scrollY));
        findState.scrollToLine = -1;
    }
    const float editorWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetNextItemWidth(editorWidth);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways;
    if (ImGui::InputTextMultiline(Tr("##document"), buffer.data(), buffer.size(), ImVec2(editorWidth, -1.0f), flags, DocumentFindInputCallback, &findState)) {
        SaveTextFile(path, std::string(buffer.data()));
        std::error_code saveEc;
        trackedMtime = fs::last_write_time(absolutePath, saveEc);
        if (textChanged) textChanged();
    }
    if (findState.documentWindow) findState.scrollY = findState.documentWindow->Scroll.y;

    const ImVec2 documentMin = ImGui::GetItemRectMin();
    const ImVec2 documentMax = ImGui::GetItemRectMax();
    const std::string findText = buffer.empty() ? std::string() : ToLower(std::string(buffer.data()));
    const std::string needle = ToLower(findState.query);
    std::vector<std::pair<int, int>> matches;
    if (!needle.empty()) {
        std::size_t offset = 0;
        while ((offset = findText.find(needle, offset)) != std::string::npos) {
            matches.emplace_back(static_cast<int>(offset), static_cast<int>(offset + needle.size()));
            offset += std::max<std::size_t>(1, needle.size());
        }
    }

    if (!matches.empty() && findState.currentMatch >= 0 && static_cast<std::size_t>(findState.currentMatch) < matches.size()) {
        const ImVec2 textOrigin(documentMin.x + ImGui::GetStyle().FramePadding.x + 1.0f,
                                documentMin.y + ImGui::GetStyle().FramePadding.y + 1.0f - findState.scrollY);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(ImVec2(documentMin.x + 1.0f, documentMin.y + 1.0f), ImVec2(documentMax.x - 2.0f, documentMax.y - 2.0f), true);
        const char* text = buffer.data();
        const float lineHeight = ImGui::GetFontSize();
        const ImU32 matchColor = ImGui::GetColorU32(ImVec4(0.90f, 0.68f, 0.16f, 0.34f));
        const ImU32 activeColor = ImGui::GetColorU32(ImVec4(1.0f, 0.55f, 0.08f, 0.62f));
        for (std::size_t i = 0; i < matches.size(); ++i) {
            int segmentStart = matches[i].first;
            const int segmentEnd = matches[i].second;
            while (segmentStart < segmentEnd) {
                int lineStart = segmentStart;
                while (lineStart > 0 && text[lineStart - 1] != '\n') --lineStart;
                int lineEnd = segmentStart;
                while (lineEnd < segmentEnd && text[lineEnd] != '\n') ++lineEnd;
                int line = 0;
                for (int at = lineStart - 1; at >= 0; --at) if (text[at] == '\n') ++line;
                const float prefixWidth = ImGui::CalcTextSize(text + lineStart, text + segmentStart).x;
                const float segmentWidth = ImGui::CalcTextSize(text + segmentStart, text + lineEnd).x;
                const ImVec2 rectMin(textOrigin.x + prefixWidth, textOrigin.y + line * lineHeight);
                const ImVec2 rectMax(rectMin.x + std::max(2.0f, segmentWidth), rectMin.y + lineHeight);
                drawList->AddRectFilled(rectMin, rectMax, static_cast<int>(i) == findState.currentMatch ? activeColor : matchColor);
                segmentStart = lineEnd + (lineEnd < segmentEnd ? 1 : 0);
            }
        }
        drawList->PopClipRect();
    }

    if (findState.open) {
        const float gap = 5.0f;
        const std::string initialCount = matches.empty() ? "0/0" :
            (findState.currentMatch < 0 ? "0/" + std::to_string(matches.size()) :
             std::to_string(findState.currentMatch + 1) + "/" + std::to_string(matches.size()));
        const float counterWidth = ImGui::CalcTextSize(initialCount.c_str()).x;
        const float availablePanelWidth = std::max(120.0f, documentMax.x - documentMin.x - 16.0f);
        const float desiredSearchWidth = 168.0f;
        const float panelWidth = std::min(desiredSearchWidth + 48.0f + counterWidth + 20.0f + gap * 4.0f + 12.0f, availablePanelWidth);
        const float rowHeight = 24.0f;
        const float panelPaddingY = 4.0f;
        const float panelHeight = rowHeight + panelPaddingY * 2.0f + 2.0f;
        const ImVec2 panelPos(documentMax.x - panelWidth - 24.0f, documentMin.y + 8.0f);
        ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, panelPaddingY));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0.0f));
        ImGui::Begin(Tr("##DocumentFindOverlay"), nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus);
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float inputHeight = ImGui::GetFrameHeight();
        const float searchWidth = std::max(64.0f, panelWidth - 48.0f - counterWidth - 20.0f - gap * 4.0f - 12.0f);
        const float inputY = rowStart.y + (rowHeight - inputHeight) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(rowStart.x, inputY));
        ImGui::SetNextItemWidth(searchWidth);
        if (findState.focusSearch) ImGui::SetKeyboardFocusHere();
        const std::string previousQuery = findState.query;
        const bool enterPressed = ImGui::InputTextWithHint(Tr("##DocumentFind"), Tr("Find..."), findState.query, sizeof(findState.query), ImGuiInputTextFlags_EnterReturnsTrue);
        const bool queryChanged = previousQuery != findState.query;
        if (queryChanged) findState.currentMatch = -1;
        const std::string currentNeedle = ToLower(findState.query);
        const std::string currentFindText = buffer.empty() ? std::string() : ToLower(std::string(buffer.data()));
        std::vector<std::pair<int, int>> currentMatches;
        if (!currentNeedle.empty()) {
            std::size_t offset = 0;
            while ((offset = currentFindText.find(currentNeedle, offset)) != std::string::npos) {
                currentMatches.emplace_back(static_cast<int>(offset), static_cast<int>(offset + currentNeedle.size()));
                offset += std::max<std::size_t>(1, currentNeedle.size());
            }
        }
        const int count = static_cast<int>(currentMatches.size());
        const float controlsX = rowStart.x + searchWidth + gap;
        ImGui::SetCursorScreenPos(ImVec2(controlsX, rowStart.y));
        const bool previousPressed = DrawFindArrowButton("##FindPrevious", true);
        ImGui::SetCursorScreenPos(ImVec2(controlsX + 24.0f + gap, rowStart.y));
        const bool nextPressed = DrawFindArrowButton("##FindNext", false);
        const bool f3Pressed = ImGui::IsKeyPressed(ImGuiKey_F3);
        const bool goPrevious = f3Pressed && ImGui::GetIO().KeyShift;
        const bool goNext = f3Pressed && !ImGui::GetIO().KeyShift;
        const bool goPreviousFromEnter = enterPressed && ImGui::GetIO().KeyShift;
        const bool goNextFromEnter = enterPressed && !ImGui::GetIO().KeyShift;
        const int updatedCount = count;
        if (updatedCount > 0 && (previousPressed || nextPressed || goPrevious || goNext || goPreviousFromEnter || goNextFromEnter)) {
            const bool movePrevious = previousPressed || goPrevious || goPreviousFromEnter;
            if (findState.currentMatch < 0) findState.currentMatch = movePrevious ? updatedCount - 1 : 0;
            else findState.currentMatch = (findState.currentMatch + (movePrevious ? updatedCount - 1 : 1)) % updatedCount;
            const int matchStart = currentMatches[static_cast<std::size_t>(findState.currentMatch)].first;
            findState.scrollToLine = static_cast<int>(std::count(buffer.begin(), buffer.begin() + matchStart, '\n'));
            findState.focusSearch = true;
        }
        const std::string matchCount = updatedCount == 0 ? "0/0" :
            (findState.currentMatch < 0 ? "0/" + std::to_string(updatedCount) : std::to_string(findState.currentMatch + 1) + "/" + std::to_string(updatedCount));
        const float counterX = controlsX + 24.0f * 2.0f + gap * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(counterX, rowStart.y + (rowHeight - ImGui::GetFontSize()) * 0.5f));
        ImGui::TextDisabled(Tr("%s"), matchCount.c_str());
        const float closeX = counterX + counterWidth + gap;
        const float closeTextY = inputY + ImGui::GetStyle().FramePadding.y - 1.5f;
        ImGui::SetCursorScreenPos(ImVec2(closeX, inputY + (inputHeight - 20.0f) * 0.5f));
        if (DrawDeleteXButton("##CloseDocumentFind", closeTextY)) findState.open = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) findState.open = false;
        findState.focusSearch = enterPressed || previousPressed || nextPressed || goPrevious || goNext || goPreviousFromEnter || goNextFromEnter;
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    ImGui::PopID();
}

struct PreviewState {
    std::string reference;
    std::string kind;
    TexturePreviewInfo info;
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    bool showSource = false;
};

struct BrowserEntry {
    fs::path path;
    std::string relative;
    bool directory = false;
    bool wad = false;
    bool dds = false;
    bool mat = false;
    bool def = false;
};

struct FolderCache {
    std::vector<BrowserEntry*> children;
};
std::unordered_map<std::string, FolderCache> g_folderCache;
std::unordered_map<std::string, bool> g_folderTextureCache;
PreviewState g_preview;
std::string g_browserRoot;
std::string g_searchIndexRoot;
bool g_searchIndexValid = false;
std::vector<BrowserEntry> g_searchEntries;
struct SearchResult {
    std::string label;
    std::string reference;
    int kind = 0;
};
std::vector<SearchResult> g_cachedSearchResults;
std::string g_cachedSearchFilter;
std::string g_pendingSearchFilter;
std::chrono::steady_clock::time_point g_searchLastEdit = std::chrono::steady_clock::now();
std::size_t g_cachedWadCount = 0;

const WadArchive* FindLoadedWad(const std::string& relativePath);
std::deque<BrowserEntry> g_browserEntries;
char g_searchBuffer[256] = {};
bool g_viewportHovered = false;
ImVec2 g_viewportPos(0.0f, 0.0f);
ImVec2 g_viewportSize(0.0f, 0.0f);
ImDrawList* g_viewportDrawList = nullptr;
bool g_showAbout = false;
bool g_showSettings = false;
GLFWwindow* g_editorInputWindow = nullptr;
int* g_rebindingKey = nullptr;

bool g_showBrowser = true;
bool g_showTexturePreview = true;
bool g_showMaterialEditor = true;
bool g_showMatCreator = false;
bool g_showDefCreator = false;
bool g_materialCreator = false;
enum class CreatorSaveKind { None, Normal, Gloss, Bump };
CreatorSaveKind g_creatorSaveKind = CreatorSaveKind::None;
int g_creatorSaveFormat = 0;
CreatorSaveKind g_creatorPendingReplaceKind = CreatorSaveKind::None;
int g_creatorPendingReplaceFormat = 0;
fs::path g_creatorPendingReplacePath;
fs::path g_pendingTextureDeletePath;
bool g_creatorOpenReplacePopup = false;
bool g_creatorOpenDeletePopup = false;
bool g_openTextureDeletePopup = false;
bool g_openNewMaterialPopup = false;
char g_newMaterialName[128] = "new_material";
fs::path g_creatorPendingDeletePath;
int g_creatorBumpHeightChannel = 1;
bool g_creatorBumpInvert = false;
float g_creatorBumpContrast = 1.0f;
float g_creatorBumpSharpness = 0.0f;
float g_creatorBumpBrightness = 0.0f;
bool g_creatorBumpNormalize = false;
bool g_creatorBumpMipmaps = true;
int g_creatorBumpFormat = 0;
fs::path g_creatorFolderBrowsePath;
std::vector<fs::path> g_creatorSaveHistory;
std::size_t g_creatorSaveHistoryIndex = 0;
char g_creatorSaveName[256] = "";
PreviewState g_creatorNormalPreview;
PreviewState g_creatorGlossPreview;
PreviewState g_creatorBumpPreview;
PreviewState g_creatorSourcePreview;
char g_materialCreatorDiffuse[256] = "";
char g_materialCreatorOutputFolder[512] = "textures";
char g_materialCreatorNormalName[128] = "new_material_norm";
char g_materialCreatorGlossName[128] = "new_material_gloss";
char g_materialCreatorBumpName[128] = "new_material";
float g_materialCreatorNormalStrength = 2.0f;
float g_materialCreatorNormalSharpness = 0.0f;
int g_materialCreatorNormalHeightChannel = 0;
bool g_materialCreatorNormalInvertHeight = false;
float g_materialCreatorNormalBlackPoint = 0.0f;
float g_materialCreatorNormalWhitePoint = 1.0f;
float g_materialCreatorNormalSmoothing = 0.0f;
int g_materialCreatorNormalFilter = 0;
bool g_materialCreatorNormalTileEdges = false;
bool g_materialCreatorFlipX = false;
bool g_materialCreatorFlipY = true;
bool g_materialCreatorFullZRange = false;
bool g_materialCreatorNormalMipmaps = true;
int g_materialCreatorNormalFormat = 0;
float g_materialCreatorGlossContrast = 1.0f;
float g_materialCreatorGlossSharpness = 0.0f;
float g_materialCreatorGlossBrightness = 0.0f;
float g_materialCreatorGlossPower = 1.0f;
bool g_materialCreatorGlossInvert = false;
float g_materialCreatorGlossLower = 0.0f;
float g_materialCreatorGlossUpper = 1.0f;
bool g_materialCreatorGlossNormalize = true;
int g_materialCreatorGlossSource = 0;
float g_materialCreatorGlossSoftness = 0.0f;
bool g_materialCreatorGlossMipmaps = true;
int g_materialCreatorGlossFormat = 0;
float g_creatorBumpBlackPoint = 0.0f;
float g_creatorBumpWhitePoint = 1.0f;
float g_creatorBumpGamma = 1.0f;
float g_creatorBumpSmoothing = 0.0f;
bool g_creatorBumpTileEdges = false;
char g_newMatFile[128] = "materials.mat";
char g_newMatTexture[256] = "";
char g_newDefFile[128] = "materials.def";
char g_newDefName[128] = "default";
char g_newDefImpactDecal[128] = "shot";
char g_newDefImpactSound[512] = "";
char g_newDefStepSound[1024] = "";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool MatchesFilter(const std::string& value, const std::string& filter) {
    if (filter.empty()) return true;
    return ToLower(value).find(ToLower(filter)) != std::string::npos;
}

void ShowTooltip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
    ImGui::TextUnformatted(Tr(text));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool DrawDeleteXButton(const char* id, float textY) {
    const ImVec2 size(20.0f, 20.0f);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 textSize = ImGui::CalcTextSize(Tr("X"));
    const ImVec2 textPos(min.x + (size.x - textSize.x) * 0.5f, textY + 3.0f);
    const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_HeaderActive : ImGuiCol_TextDisabled);
    ImGui::GetWindowDrawList()->AddText(textPos, color, "X");
    return pressed;
}

constexpr float kBrowserRowHeight = 20.0f;

bool DrawBrowserTreeNode(const char* label, ImGuiTreeNodeFlags flags) {
    const float verticalPadding = std::max(0.0f, (kBrowserRowHeight - ImGui::GetFontSize()) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, verticalPadding));
    const bool open = ImGui::TreeNodeEx(label, flags | ImGuiTreeNodeFlags_FramePadding);
    ImGui::PopStyleVar();
    return open;
}

bool DrawBrowserSelectable(const char* label, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None,
                           const ImVec2& size = ImVec2(0.0f, kBrowserRowHeight)) {
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    const bool pressed = ImGui::Selectable(label, selected, flags, size);
    ImGui::PopStyleVar();
    return pressed;
}

bool IsExtension(const fs::path& path, const char* ext) {
    return ToLower(path.extension().string()) == ext;
}

const WadArchive* FindLoadedWad(const std::string& relativePath) {
    for (const auto& wad : GetWadArchives()) {
        if (ToLower(wad.relativePath) == ToLower(relativePath)) return &wad;
    }
    return nullptr;
}

void SaveWads(EditorConfig& cfg) {
    cfg.loadedWads = GetLoadedWadPaths();
    SaveConfig(cfg);
}

void ReleasePreview() {
    ReleaseTexturePreview(g_preview.info);
    g_preview.reference.clear();
    g_preview.kind.clear();
}

void ReleasePreviewForPath(const fs::path& path) {
    if (g_preview.reference.empty() || g_preview.reference.rfind("wad://", 0) == 0 ||
        g_preview.reference.rfind("wad:/", 0) == 0) return;
    fs::path previewPath(g_preview.reference);
    if (previewPath.is_relative()) previewPath = gameRootPath / previewPath;
    if (previewPath.lexically_normal() == path.lexically_normal()) ReleasePreview();
}

void SelectTexture(const std::string& reference, const std::string& kind) {
    if (reference == g_preview.reference && kind == g_preview.kind) return;
    if (reference.empty()) return;

    TexturePreviewInfo next = LoadTexturePreview(reference);
    if (!next.valid || next.texture == 0) {
        ReleaseTexturePreview(next);
        return;
    }

    ReleasePreview();
    g_preview.info = next;
    g_preview.reference = reference;
    g_preview.kind = kind;
}

fs::path ResolveSkyboxEnvPath(const fs::path& root) {
    std::error_code ec;
    if (root.empty()) return {};
    fs::path candidate = root / "gfx" / "env";
    if (fs::is_directory(candidate, ec)) return candidate;
    ec.clear();
    if (root.filename() == "env" && fs::is_directory(root, ec)) return root;
    ec.clear();
    if (root.filename() == "gfx" && fs::is_directory(root / "env", ec)) return root / "env";
    ec.clear();
    if (fs::is_directory(root / "env", ec)) return root / "env";
    return {};
}

void PopulateBrowserFolder(const fs::path& dir) {
    const std::string key = dir.generic_string();
    if (g_folderCache.find(key) != g_folderCache.end()) return;
    FolderCache cache;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        const fs::path path = it->path();
        std::error_code typeEc;
        if (it->is_directory(typeEc) && !typeEc) {
            const std::size_t index = g_browserEntries.size();
            g_browserEntries.push_back({path, fs::relative(path, gameRootPath, ec).generic_string(), true, false, false, false, false});
            ec.clear();
            cache.children.push_back(&g_browserEntries[index]);
        } else if (it->is_regular_file(typeEc) && !typeEc) {
            const bool wad = IsExtension(path, ".wad");
            const bool dds = IsExtension(path, ".dds");
            const bool def = IsExtension(path, ".def");
            if (wad || dds || def) {
                const std::size_t index = g_browserEntries.size();
                const std::string relative = fs::relative(path, gameRootPath, ec).generic_string();
                ec.clear();
                g_browserEntries.push_back({path, relative, false, wad, dds, false, def});
                cache.children.push_back(&g_browserEntries[index]);
            }
        }
        it.increment(ec);
    }

    std::sort(cache.children.begin(), cache.children.end(), [](BrowserEntry* a, BrowserEntry* b) {
        if (a->directory != b->directory) return a->directory > b->directory;
        return ToLower(a->path.filename().string()) < ToLower(b->path.filename().string());
    });

    g_folderCache.emplace(key, std::move(cache));
}

void RebuildBrowser() {
    const std::string root = gameRootPath.string();
    if (root == g_browserRoot && !g_folderCache.empty()) return;
    g_browserRoot = root;
    g_browserEntries.clear();
    g_folderCache.clear();
    g_folderTextureCache.clear();
    g_searchEntries.clear();
    g_searchIndexRoot.clear();
    g_searchIndexValid = false;
    if (gameRootPath.empty() || !fs::exists(gameRootPath) || !fs::is_directory(gameRootPath)) return;
    PopulateBrowserFolder(gameRootPath);
}


void InvalidateBrowserCaches() {
    g_browserRoot.clear();
    g_browserEntries.clear();
    g_folderCache.clear();
    g_folderTextureCache.clear();
    g_searchEntries.clear();
    g_searchIndexRoot.clear();
    g_searchIndexValid = false;
    g_cachedSearchResults.clear();
    g_cachedSearchFilter.clear();
    g_pendingSearchFilter.clear();
    g_cachedWadCount = GetWadArchives().size();
    RebuildBrowser();
}

void RequestDeleteTexture(const fs::path& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec) || ToLower(path.extension().string()) != ".dds") return;
    g_pendingTextureDeletePath = path;
    g_openTextureDeletePopup = true;
}

void RequestCreatorDeleteTexture(const fs::path& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec) || ToLower(path.extension().string()) != ".dds") return;
    g_creatorPendingDeletePath = path;
    g_creatorOpenDeletePopup = true;
}

void DrawCreatorDeletePopup() {
    if (g_creatorOpenDeletePopup) {
        g_creatorOpenDeletePopup = false;
        ImGui::OpenPopup(Tr("##CreatorDeleteTexture"));
    }
    static ImVec2 lastViewportSize(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    if (viewportSize.x != lastViewportSize.x || viewportSize.y != lastViewportSize.y) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        lastViewportSize = viewportSize;
    }
    if (!BeginSpacedModal("##CreatorDeleteTexture", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    ImGui::TextUnformatted(Tr("Delete texture?"));
    ImGui::TextWrapped(Tr("%s"), g_creatorPendingDeletePath.filename().string().c_str());
    ImGui::Spacing();
    const float width = 108.0f;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float row = width * 2.0f + gap;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowContentRegionMax().x - row) * 0.5f));
    if (ImGui::Button(Tr("DELETE"), ImVec2(width, 32.0f))) {
        std::error_code ec;
        fs::remove(g_creatorPendingDeletePath, ec);
        if (!ec) InvalidateBrowserCaches();
        g_creatorPendingDeletePath.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("CANCEL"), ImVec2(width, 32.0f))) {
        g_creatorPendingDeletePath.clear();
        ImGui::CloseCurrentPopup();
    }
    EndSpacedModal();
}

void AssignTexture(Material& material, const char* field, const std::string& reference);

std::string GetNewMaterialNameFromBrowserSelection() {
    if (g_preview.reference.empty()) return "new_material";

    std::string selected = g_preview.reference;
    if (selected.rfind("wad://", 0) == 0 || selected.rfind("wad:/", 0) == 0) {
        const std::size_t separator = selected.rfind('#');
        if (separator != std::string::npos) selected = selected.substr(separator + 1);
    }

    const fs::path selectedPath(selected);
    std::string name = selectedPath.filename().string();
    if (name.empty()) name = selected;
    const std::string extension = ToLower(fs::path(name).extension().string());
    if (extension == ".dds" || extension == ".png" || extension == ".tga") {
        name = fs::path(name).stem().string();
    }
    return name.empty() ? "new_material" : name;
}

std::string GetSelectedTextureStem() {
    std::string selected = g_preview.reference;
    if (selected.rfind("wad://", 0) == 0 || selected.rfind("wad:/", 0) == 0) {
        const std::size_t separator = selected.rfind('#');
        if (separator != std::string::npos) selected = selected.substr(separator + 1);
    }
    const fs::path selectedPath(selected);
    std::string stem = selectedPath.stem().string();
    if (stem.empty()) stem = selectedPath.filename().string();
    return stem;
}

void AutoAssignMaterialTextures(Material& material) {
    RefreshMaterialTextureIndex();
    AutoAssignMaterialTexturesByName(material);
    const std::string stem = fs::path(material.name).stem().string();
    const std::string selectedStem = GetSelectedTextureStem();
    if (material.diffusePath[0] == '\0' && !g_preview.reference.empty() &&
        ToLower(selectedStem) == ToLower(stem)) {
        AssignTexture(material, "diffuse", g_preview.reference);
        material.syncParams();
    }
}

void DrawNewMaterialPopup(const std::string& currentFileName, std::vector<Material>& materials, int& currentMatIndex, EditorConfig& editorCfg) {
    if (g_openNewMaterialPopup) {
        g_openNewMaterialPopup = false;
        const std::string suggestedName = GetNewMaterialNameFromBrowserSelection();
        std::snprintf(g_newMaterialName, sizeof(g_newMaterialName), "%s", suggestedName.c_str());
        ImGui::OpenPopup(Tr("##NewMaterial"));
    }
    static ImVec2 lastViewportSize(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    if (viewportSize.x != lastViewportSize.x || viewportSize.y != lastViewportSize.y) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        lastViewportSize = viewportSize;
    }
    if (!BeginSpacedModal("##NewMaterial", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    ImGui::TextUnformatted(Tr("NEW MATERIAL"));
    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputText(Tr("##NewMaterialName"), g_newMaterialName, sizeof(g_newMaterialName), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    const float buttonWidth = 112.0f;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float rowWidth = buttonWidth * 2.0f + gap;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowContentRegionMax().x - rowWidth) * 0.5f));
    const bool canCreate = g_newMaterialName[0] != '\0';
    if (!canCreate) ImGui::BeginDisabled();
    if (ImGui::Button(Tr("CREATE"), ImVec2(buttonWidth, 32.0f))) {
        const std::string requestedName = g_newMaterialName;
        const std::string normalizedName = ToLower(requestedName);
        const auto existing = std::find_if(materials.begin(), materials.end(), [&](const Material& material) {
            return ToLower(material.name) == normalizedName;
        });
        if (existing != materials.end()) {
            currentMatIndex = static_cast<int>(std::distance(materials.begin(), existing));
            Material& material = materials[currentMatIndex];
            if (editorCfg.autoAssignMaterialTextures) {
                AutoAssignMaterialTextures(material);
                material.syncParams();
            }
            material.loadTextures();
        } else {
            Material newMat;
            newMat.name = requestedName;
            newMat.smoothness = 1.0f;
            newMat.reflectScale = 0.3f;
            if (editorCfg.autoAssignMaterialTextures) AutoAssignMaterialTextures(newMat);
            newMat.syncParams();
            materials.push_back(std::move(newMat));
            currentMatIndex = static_cast<int>(materials.size()) - 1;
            materials[currentMatIndex].loadTextures();
        }
        SaveMaterialsAndRefreshText(currentFileName, materials);
        ImGui::CloseCurrentPopup();
    }
    if (!canCreate) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(Tr("CANCEL"), ImVec2(buttonWidth, 32.0f))) ImGui::CloseCurrentPopup();
    EndSpacedModal();
}

void DrawTextureDeletePopup() {
    if (g_openTextureDeletePopup) {
        g_openTextureDeletePopup = false;
        ImGui::OpenPopup(Tr("##DeleteTextureConfirm"));
    }
    static ImVec2 lastViewportSize(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    if (viewportSize.x != lastViewportSize.x || viewportSize.y != lastViewportSize.y) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        lastViewportSize = viewportSize;
    }
    if (!BeginSpacedModal("##DeleteTextureConfirm", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    ImGui::TextUnformatted(Tr("Delete texture?"));
    ImGui::TextWrapped(Tr("%s"), g_pendingTextureDeletePath.filename().string().c_str());
    ImGui::Spacing();
    const float buttonWidth = 112.0f;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float rowWidth = buttonWidth * 2.0f + gap;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowContentRegionMax().x - rowWidth) * 0.5f));
    if (ImGui::Button(Tr("DELETE"), ImVec2(buttonWidth, 32.0f))) {
        std::error_code ec;
        const fs::path deleted = g_pendingTextureDeletePath;
        if (fs::remove(deleted, ec) && !ec) {
            std::error_code relEc;
            const fs::path relative = fs::relative(deleted, gameRootPath, relEc);
            if (!relEc && g_preview.reference == relative.generic_string()) ReleasePreview();
            InvalidateBrowserCaches();
        }
        g_pendingTextureDeletePath.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("CANCEL"), ImVec2(buttonWidth, 32.0f))) {
        g_pendingTextureDeletePath.clear();
        ImGui::CloseCurrentPopup();
    }
    EndSpacedModal();
}


std::string CreatorSelectedTextureFolder() {
    if (g_preview.reference.empty()) return {};
    if (g_preview.reference.rfind("wad://", 0) == 0 || g_preview.reference.rfind("wad:/", 0) == 0) return {};
    fs::path selected = fs::path(g_preview.reference);
    if (!selected.is_absolute()) selected = gameRootPath / selected;
    std::error_code ec;
    if (fs::is_regular_file(selected, ec)) return fs::relative(selected.parent_path(), gameRootPath, ec).generic_string();
    return {};
}

void ReleaseCreatorPreview(PreviewState& preview);
void SetCreatorDiffuse(const std::string& reference);
void GenerateCreatorNormalPreview();
void GenerateCreatorGlossPreview();
void GenerateCreatorBumpPreview();

void EnterMaterialCreator(std::vector<Material>& materials, int& currentMatIndex, const std::string& currentFileName) {
    if (g_materialCreator) return;
    if (g_preview.reference.empty() && !materials.empty() && currentMatIndex >= 0 && static_cast<std::size_t>(currentMatIndex) < materials.size()) {
        if (materials[currentMatIndex].diffusePath[0]) SetCreatorDiffuse(materials[currentMatIndex].diffusePath);
    } else if (!g_preview.reference.empty()) {
        SetCreatorDiffuse(g_preview.reference);
    }
    g_materialCreator = true;
    g_showBrowser = true;
    g_showTexturePreview = false;
    g_showMaterialEditor = false;
    g_showSettings = false;
    g_showAbout = false;
    if (g_materialCreatorDiffuse[0]) {
        GenerateCreatorNormalPreview();
        GenerateCreatorGlossPreview();
        GenerateCreatorBumpPreview();
    }
}

std::string CreatorRelativePath(const fs::path& path) {
    std::error_code ec;
    if (path.is_relative()) return path.generic_string();
    const fs::path relative = fs::relative(path, gameRootPath, ec);
    if (!ec && !relative.empty() && relative.generic_string().rfind("..", 0) != 0) return relative.generic_string();
    return path.generic_string();
}

bool CreatorPathInsideGame(const fs::path& path) {
    std::error_code ec;
    const fs::path canonicalRoot = fs::weakly_canonical(gameRootPath, ec);
    ec.clear();
    const fs::path canonicalPath = fs::weakly_canonical(path, ec);
    if (ec) return false;
    const std::string root = canonicalRoot.generic_string();
    const std::string candidate = canonicalPath.generic_string();
    return candidate == root || (candidate.size() > root.size() && candidate.rfind(root + "/", 0) == 0);
}

void SelectMat(const std::string& path, std::vector<Material>& materials, int& currentMatIndex,
               std::string& currentFileName, bool autoAssignTextures) {
    if (currentFileName == path && !materials.empty()) return;
    if (!LoadAllMaterials(path, materials, autoAssignTextures)) return;
    currentFileName = path;
    currentMatIndex = materials.empty() ? -1 : 0;
    if (currentMatIndex >= 0) materials[currentMatIndex].loadTextures();
}

void SelectDef(const std::string& path, std::vector<PhysicalMaterialEntry>& physicalMaterials, int& currentPhysMatIndex, std::string& currentDefFile) {
    LoadAllPhysicalMaterials(path, physicalMaterials);
    currentDefFile = path;
    currentPhysMatIndex = physicalMaterials.empty() ? -1 : 0;
    if (currentPhysMatIndex >= 0) physicalMaterials[currentPhysMatIndex].updateBuffers();
}

void ReloadMaterialsPreserveSelection(const std::string& path, std::vector<Material>& materials,
                                      int& currentMatIndex, bool autoAssignTextures) {
    const std::string selectedName = currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size() ? materials[currentMatIndex].name : std::string();
    std::vector<Material> fresh;
    if (!LoadAllMaterials(path, fresh, autoAssignTextures)) return;
    for (auto& material : materials) material.releaseTextures();
    materials = std::move(fresh);
    currentMatIndex = -1;
    for (size_t i = 0; i < materials.size(); ++i) {
        if (materials[i].name == selectedName) { currentMatIndex = static_cast<int>(i); break; }
    }
    if (currentMatIndex < 0 && !materials.empty()) currentMatIndex = 0;
    if (currentMatIndex >= 0) materials[currentMatIndex].loadTextures();
}

void ReloadPhysicalMaterialsPreserveSelection(const std::string& path, std::vector<PhysicalMaterialEntry>& physicalMaterials, int& currentPhysMatIndex) {
    const std::string selectedName = currentPhysMatIndex >= 0 && static_cast<size_t>(currentPhysMatIndex) < physicalMaterials.size() ? physicalMaterials[currentPhysMatIndex].name : std::string();
    std::vector<PhysicalMaterialEntry> fresh;
    LoadAllPhysicalMaterials(path, fresh);
    physicalMaterials = std::move(fresh);
    currentPhysMatIndex = -1;
    for (size_t i = 0; i < physicalMaterials.size(); ++i) {
        if (physicalMaterials[i].name == selectedName) { currentPhysMatIndex = static_cast<int>(i); break; }
    }
    if (currentPhysMatIndex < 0 && !physicalMaterials.empty()) currentPhysMatIndex = 0;
    if (currentPhysMatIndex >= 0) physicalMaterials[currentPhysMatIndex].updateBuffers();
}

void AssignTexture(Material& material, const char* field, const std::string& reference) {
    char* target = nullptr;
    std::size_t size = 0;
    if (std::strcmp(field, "diffuse") == 0) { target = material.diffusePath; size = sizeof(material.diffusePath); }
    else if (std::strcmp(field, "normal") == 0) { target = material.normalPath; size = sizeof(material.normalPath); }
    else if (std::strcmp(field, "gloss") == 0) { target = material.glossPath; size = sizeof(material.glossPath); }
    else if (std::strcmp(field, "luma") == 0) { target = material.lumaPath; size = sizeof(material.lumaPath); }
    else if (std::strcmp(field, "bump") == 0) { target = material.bumpPath; size = sizeof(material.bumpPath); }
    else if (std::strcmp(field, "detail") == 0) { target = material.detailPath; size = sizeof(material.detailPath); }
    if (!target || size == 0) return;

    std::string value = reference;
    if (value.rfind("wad://", 0) == 0) {
        const std::size_t separator = value.rfind('#');
        if (separator != std::string::npos) value = value.substr(separator + 1);
        const std::string extension = ToLower(fs::path(value).extension().string());
        if (extension == ".png" || extension == ".tga" || extension == ".dds") value = fs::path(value).stem().string();
    } else if (value.rfind("wad:/", 0) == 0) {
        value = value.substr(5);
        const std::size_t slash = value.find_last_of("/\\");
        if (slash != std::string::npos) value = value.substr(slash + 1);
        const std::string extension = ToLower(fs::path(value).extension().string());
        if (extension == ".png" || extension == ".tga" || extension == ".dds") value = fs::path(value).stem().string();
    } else {
        const std::string extension = ToLower(fs::path(value).extension().string());
        if (extension == ".png" || extension == ".tga") return;
        if (extension == ".dds") {
            fs::path path(value);
            value = path.generic_string();
        }
    }

    if (size > 0) std::snprintf(target, size, "%s", value.c_str());
}


void AssignSelected(Material* material, const char* field, const std::string& currentFileName,
                    const std::vector<Material>& materials) {
    if (!material || g_preview.reference.empty() || !g_preview.info.valid || g_preview.info.texture == 0) return;
    AssignTexture(*material, field, g_preview.reference);
    material->syncParams();
    material->loadTextures();
    SaveMaterialsAndRefreshText(currentFileName, materials);
}

void DrawAssignGrid(Material* material, const std::string& currentFileName,
                    const std::vector<Material>& materials) {
    ImGui::TextDisabled(Tr("ASSIGN SELECTED TEXTURE AS"));
    const char* labels[] = {
        Tr("DIFFUSE"), Tr("NORMAL"), Tr("GLOSS"), Tr("LUMA"), Tr("BUMP"), Tr("DETAIL")
    };
    const char* fields[] = {"diffuse", "normal", "gloss", "luma", "bump", "detail"};
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    for (int i = 0; i < 6; ++i) {
        if ((i & 1) != 0) ImGui::SameLine();
        if (ImGui::Button(labels[i], ImVec2(std::max(60.0f, width), 22.0f))) {
            AssignSelected(material, fields[i], currentFileName, materials);
        }
    }
}

void DrawPreview(Material* material, float width, const std::string& currentFileName,
                 const std::vector<Material>& materials) {
    ImGui::BeginChild(Tr("TexturePreviewContent"), ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);

    if (g_preview.reference.empty()) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(std::max(8.0f, (avail.x - 150.0f) * 0.5f), std::max(20.0f, (avail.y - 20.0f) * 0.5f)));
        ImGui::TextDisabled(Tr("CLICK A TEXTURE"));
        ImGui::EndChild();
        return;
    }

    ImGui::Text(Tr("%s"), g_preview.reference.c_str());
    ImGui::TextDisabled("%s", Tr(g_preview.kind.c_str()));
    if (g_preview.info.valid) {
        ImGui::TextDisabled(Tr("%d x %d"), g_preview.info.width, g_preview.info.height);
        ImGui::Spacing();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float rowHeight = 22.0f;
        const float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
        const float assignHeight = ImGui::GetTextLineHeightWithSpacing() +
                       (rowHeight + itemSpacingY) * 3.0f + itemSpacingY;
        const float imageAreaHeight = std::clamp(avail.y - assignHeight, 1.0f, 220.0f);
        ImGui::BeginChild(Tr("##TextureImageArea"), ImVec2(0.0f, imageAreaHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 imageAvail = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(g_preview.info.width) /
                             static_cast<float>(std::max(1, g_preview.info.height));
        float imageW = std::max(1.0f, imageAvail.x - 8.0f);
        float imageH = imageW / std::max(0.01f, aspect);
        if (imageH > imageAvail.y - 4.0f) {
            imageH = std::max(1.0f, imageAvail.y - 4.0f);
            imageW = imageH * aspect;
        }
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + std::max(4.0f, (imageAvail.x - imageW) * 0.5f),
                                   cursor.y + std::max(4.0f, (imageAvail.y - imageH) * 0.5f)));
        ImGui::Image(static_cast<ImTextureID>(g_preview.info.texture), ImVec2(imageW, imageH));
        ImGui::EndChild();
    } else {
        ImGui::BeginChild(Tr("NoPreview"), ImVec2(0, 100.0f), true);
        ImGui::TextDisabled(Tr("PREVIEW UNAVAILABLE"));
        ImGui::EndChild();
    }

    if (material) DrawAssignGrid(material, currentFileName, materials);
    else ImGui::TextDisabled(Tr("LOAD A .MAT FILE TO ASSIGN TEXTURES"));
    (void)width;
    ImGui::EndChild();
}

void DrawWadNode(const WadArchive& wad, const std::string& filter) {
    const bool hasMatch = filter.empty() || std::any_of(wad.textures.begin(), wad.textures.end(), [&](const WadTexture& tex) {
        return MatchesFilter(tex.name, filter) || MatchesFilter(MakeWadTextureReference(wad, tex), filter);
    });
    if (!hasMatch) return;

    const std::string id = std::string(Tr("WAD")) + "  " + wad.displayName + "  [" + std::to_string(wad.textures.size()) + "]##" + wad.relativePath;
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | (!filter.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::Indent(4.0f);
    const bool open = DrawBrowserTreeNode(id.c_str(), flags);
    ImGui::Unindent(4.0f);
    if (!open) return;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(wad.textures.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& texture = wad.textures[static_cast<std::size_t>(i)];
            const std::string ref = MakeWadTextureReference(wad, texture);
            if (!MatchesFilter(texture.name, filter) && !MatchesFilter(ref, filter)) continue;
            const std::string label = std::string(Tr("TEXTURE")) + "  " + texture.name + "##" + ref;
            ImGui::Indent(4.0f);
            if (DrawBrowserSelectable(label.c_str(), g_preview.reference == ref)) SelectTexture(ref, "WAD");
            ImGui::Unindent(4.0f);
        }
    }
    ImGui::TreePop();
}

bool FolderContainsTexture(const fs::path& dir) {
    const std::string key = dir.generic_string();
    auto cached = g_folderTextureCache.find(key);
    if (cached != g_folderTextureCache.end()) return cached->second;

    std::error_code ec;
    bool found = false;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code typeEc;
        if (!it->is_regular_file(typeEc) || typeEc) continue;
        const fs::path path = it->path();
        if (IsExtension(path, ".dds") || IsExtension(path, ".wad") || IsExtension(path, ".png") || IsExtension(path, ".tga")) {
            found = true;
            break;
        }
    }
    g_folderTextureCache[key] = found;
    return found;
}

void DrawDirectory(const fs::path&, const std::string&, EditorConfig&, std::vector<Material>&, std::vector<PhysicalMaterialEntry>&, std::string&, int&, std::string&, int&);

void BuildSearchIndex() {
    const std::string root = gameRootPath.string();
    if (g_searchIndexValid && g_searchIndexRoot == root) return;

    g_searchEntries.clear();
    g_searchIndexRoot = root;
    g_searchIndexValid = true;
    if (gameRootPath.empty() || !fs::is_directory(gameRootPath)) return;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(gameRootPath, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code typeEc;
        if (!it->is_regular_file(typeEc) || typeEc) continue;
        const fs::path path = it->path();
        const bool wad = IsExtension(path, ".wad");
        const bool dds = IsExtension(path, ".dds");
        const bool def = IsExtension(path, ".def");
        if (!wad && !dds && !def) continue;
        std::error_code relEc;
        const std::string relative = fs::relative(path, gameRootPath, relEc).generic_string();
        if (relEc) continue;
        g_searchEntries.push_back({path, relative, false, wad, dds, false, def});
    }
}

void DrawSearchResults(const std::string& filter, EditorConfig& cfg, std::vector<Material>& materials,
                       std::vector<PhysicalMaterialEntry>& physicalMaterials, std::string& currentFileName,
                       int& currentMatIndex, std::string& currentDefFile, int& currentPhysMatIndex) {
    if (filter.empty()) {
        DrawDirectory(gameRootPath, filter, cfg, materials, physicalMaterials, currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
        return;
    }

    BuildSearchIndex();
    const auto now = std::chrono::steady_clock::now();
    if (filter != g_cachedSearchFilter && filter != g_pendingSearchFilter) {
        g_pendingSearchFilter = filter;
        g_searchLastEdit = now;
    }
    const bool ready = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_searchLastEdit).count() >= 140;
    const std::size_t wadCount = GetWadArchives().size();
    if (filter != g_cachedSearchFilter || wadCount != g_cachedWadCount) {
        if (ready || wadCount != g_cachedWadCount) {
            g_cachedSearchResults.clear();
            g_cachedSearchFilter = filter;
            g_cachedWadCount = wadCount;
            constexpr std::size_t maxResults = 500;
            for (const WadArchive& wad : GetWadArchives()) {
                for (const WadTexture& texture : wad.textures) {
                    if (g_cachedSearchResults.size() >= maxResults) break;
                    const std::string ref = MakeWadTextureReference(wad, texture);
                    if (!MatchesFilter(texture.name, filter) && !MatchesFilter(ref, filter)) continue;
                    g_cachedSearchResults.push_back({
                        "TEXTURE  " + texture.name + "  [" + wad.relativePath + "]", ref, 0
                    });
                }
                if (g_cachedSearchResults.size() >= maxResults) break;
            }
            for (const BrowserEntry& entry : g_searchEntries) {
                if (g_cachedSearchResults.size() >= maxResults) break;
                const bool fileMatch = MatchesFilter(entry.path.filename().string(), filter) || MatchesFilter(entry.relative, filter);
                if (!fileMatch) continue;
                if (entry.wad) {
                    if (!FindLoadedWad(entry.relative)) {
                        g_cachedSearchResults.push_back({"WAD  " + entry.relative + "  [CLICK TO LOAD]", entry.relative, 1});
                    }
                    continue;
                }
                const std::string prefix = entry.dds ? "DDS  " : "DEF  ";
                g_cachedSearchResults.push_back({prefix + entry.relative, entry.relative, entry.dds ? 2 : 3});
            }
        }
    }

    if (filter != g_cachedSearchFilter) return;

    for (const SearchResult& result : g_cachedSearchResults) {
        if (result.kind == 0) {
            if (DrawBrowserSelectable((result.label + "##SEARCHWAD:" + result.reference).c_str(), g_preview.reference == result.reference)) {
                SelectTexture(result.reference, "WAD");
            }
        } else if (result.kind == 1) {
            if (DrawBrowserSelectable((result.label + "##SEARCHWADFILE:" + result.reference).c_str(), false)) {
                if (AddWadArchive(result.reference)) SaveWads(cfg);
            }
        } else if (result.kind == 2) {
            ImGui::PushID(("SEARCHDDSROW:" + result.reference).c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            const float xWidth = 20.0f;
            const float rowTextY = ImGui::GetCursorScreenPos().y;
            const float rowY = ImGui::GetCursorPosY();
            const float rowHeight = ImGui::GetFrameHeight();
            const float available = ImGui::GetContentRegionAvail().x;
            ImGui::BeginGroup();
            if (DrawBrowserSelectable(result.label.c_str(), g_preview.reference == result.reference, ImGuiSelectableFlags_None,
                                      ImVec2(std::max(1.0f, available - xWidth), kBrowserRowHeight))) SelectTexture(result.reference, "DDS");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosY(rowY + (rowHeight - 20.0f) * 0.5f);
            if (DrawDeleteXButton("##DeleteSearch", rowTextY)) RequestDeleteTexture(gameRootPath / result.reference);
            ImGui::EndGroup();
            ImGui::PopStyleVar();
            ImGui::PopID();
        } else {
            if (DrawBrowserSelectable((result.label + "##SEARCHDEF:" + result.reference).c_str(), currentDefFile == result.reference)) {
                SelectDef(result.reference, physicalMaterials, currentPhysMatIndex, currentDefFile);
            }
        }
    }
    if (g_cachedSearchResults.empty() && ready) ImGui::TextDisabled(Tr("NO SEARCH RESULTS"));
}

void DrawDirectory(const fs::path& dir, const std::string& filter, EditorConfig& cfg, std::vector<Material>& materials,
                   std::vector<PhysicalMaterialEntry>& physicalMaterials, std::string& currentFileName, int& currentMatIndex,
                   std::string& currentDefFile, int& currentPhysMatIndex) {
    if (!filter.empty()) {
        DrawSearchResults(filter, cfg, materials, physicalMaterials, currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
        return;
    }


    PopulateBrowserFolder(dir);
    auto it = g_folderCache.find(dir.generic_string());
    if (it == g_folderCache.end()) return;
    auto& children = it->second.children;
    if (children.empty()) return;

    const std::string dirLabel = std::string("FOLDER  ") + (dir == gameRootPath ? gameRootPath.filename().string() : dir.filename().string()) + "##DIR:" + dir.generic_string();
    ImGui::PushID(dir.generic_string().c_str());
    const ImGuiTreeNodeFlags rootFlags = (dir == gameRootPath || !filter.empty()) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    ImGui::Indent(4.0f);
    const bool open = DrawBrowserTreeNode(dirLabel.c_str(), rootFlags | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::Unindent(4.0f);
    if (!open) {
        ImGui::PopID();
        return;
    }

    std::size_t directoryCount = 0;
    while (directoryCount < children.size() && children[directoryCount]->directory) {
        ++directoryCount;
    }

    for (std::size_t i = 0; i < directoryCount; ++i) {
        const BrowserEntry* entry = children[i];
        if (entry->path != gameRootPath && !FolderContainsTexture(entry->path)) continue;
        DrawDirectory(entry->path, filter, cfg, materials, physicalMaterials, currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
    }

    for (std::size_t i = directoryCount; i < children.size(); ++i) {
        const BrowserEntry* entry = children[i];
        if (entry->wad) {
            const WadArchive* loaded = FindLoadedWad(entry->relative);
            if (loaded) {
                DrawWadNode(*loaded, filter);
            } else if (MatchesFilter(entry->relative, filter) || MatchesFilter(entry->path.filename().string(), filter)) {
                const std::string label = std::string(Tr("WAD")) + "  " + entry->path.filename().string() + "  [" + Tr("CLICK TO LOAD") + "]##" + entry->relative;
                ImGui::Indent(4.0f);
                if (DrawBrowserSelectable(label.c_str(), false)) {
                    if (AddWadArchive(entry->relative)) SaveWads(cfg);
                }
                ImGui::Unindent(4.0f);
            }
        } else if (entry->dds) {
            ImGui::PushID(("DDSROW:" + entry->relative).c_str());
            ImGui::Indent(4.0f);
            const std::string label = std::string(Tr("DDS")) + "  " + entry->path.filename().string();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            const float xWidth = 20.0f;
            const float rowTextY = ImGui::GetCursorScreenPos().y;
            const float rowY = ImGui::GetCursorPosY();
            const float rowHeight = kBrowserRowHeight;
            const float available = ImGui::GetContentRegionAvail().x;
            ImGui::BeginGroup();
            if (DrawBrowserSelectable(label.c_str(), g_preview.reference == entry->relative, ImGuiSelectableFlags_None,
                                      ImVec2(std::max(1.0f, available - xWidth), kBrowserRowHeight))) SelectTexture(entry->relative, "DDS");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosY(rowY + (rowHeight - 20.0f) * 0.5f);
            if (DrawDeleteXButton("##DeleteDDS", rowTextY)) RequestDeleteTexture(entry->path);
            ImGui::EndGroup();
            ImGui::PopStyleVar();
            ImGui::Unindent(4.0f);
            ImGui::PopID();
        } else if (entry->def) {
            const std::string label = std::string(Tr("DEF")) + "  " + entry->path.filename().string() + "##" + entry->relative;
            ImGui::Indent(4.0f);
            if (DrawBrowserSelectable(label.c_str(), currentDefFile == entry->relative)) SelectDef(entry->relative, physicalMaterials, currentPhysMatIndex, currentDefFile);
            ImGui::Unindent(4.0f);
        }
    }

    ImGui::TreePop();
    ImGui::PopID();
}

const char* KeyName(int key) {
    switch (key) {
        case GLFW_KEY_SPACE: return "Space";
        case GLFW_KEY_LEFT_SHIFT: return "Left Shift";
        case GLFW_KEY_LEFT_CONTROL: return "Left Ctrl";
        case GLFW_KEY_LEFT_ALT: return "Left Alt";
        case GLFW_KEY_RIGHT_SHIFT: return "Right Shift";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_RIGHT_ALT: return "Right Alt";
        case GLFW_KEY_TAB: return "Tab";
        case GLFW_KEY_ENTER: return "Enter";
        case GLFW_KEY_ESCAPE: return "Esc";
        case GLFW_KEY_BACKSPACE: return "Backspace";
        case GLFW_KEY_DELETE: return "Delete";
        case GLFW_KEY_INSERT: return "Insert";
        case GLFW_KEY_UP: return "Up";
        case GLFW_KEY_DOWN: return "Down";
        case GLFW_KEY_LEFT: return "Left";
        case GLFW_KEY_RIGHT: return "Right";
        default: break;
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        static char name[2];
        name[0] = static_cast<char>('A' + key - GLFW_KEY_A);
        name[1] = '\0';
        return name;
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        static char name[2];
        name[0] = static_cast<char>('0' + key - GLFW_KEY_0);
        name[1] = '\0';
        return name;
    }
    return "Unassigned";
}

bool DrawResettableInput(const char* id, const char* label, char* buffer, size_t bufferSize, bool& visible) {
    const float buttonWidth = 24.0f;
    const float gap = 4.0f;
    const float labelWidth = 54.0f;
    const float cellWidth = ImGui::GetContentRegionAvail().x;
    const float inputWidth = std::max(32.0f, cellWidth - labelWidth - buttonWidth * 2.0f - gap * 3.0f);
    const float startX = ImGui::GetCursorPosX();
    bool changed = false;

    ImGui::PushID(id);
    ImGui::TextUnformatted(Tr(label));
    ImGui::SameLine();
    ImGui::SetCursorPosX(startX + labelWidth);
    if (ImGui::Button(Tr("X##reset"), ImVec2(buttonWidth, 0.0f))) {
        if (buffer[0] != '\0') {
            buffer[0] = '\0';
            changed = true;
        }
    }
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button(visible ? Tr("S##visibility") : Tr("H##visibility"), ImVec2(buttonWidth, 0.0f))) visible = !visible;
    ShowTooltip(visible ? "Show texture in the viewport." : "Hide texture in the viewport.");
    ImGui::SameLine(0.0f, gap);
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputText(Tr("##value"), buffer, bufferSize)) changed = true;
    ImGui::PopID();
    return changed;
}


void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
}

void SetTheme(EditorConfig& cfg, const std::string& name) {
    ImGuiStyle& style = ImGui::GetStyle();
    SetupStyle();
    ImVec4* c = style.Colors;
    const bool pink = name == "Pastel Pink";
    const bool green = name == "Pastel Green";
    if (name == "AMOLED") {
        const ImVec4 window(0.0f, 0.0f, 0.0f, 1.0f);
        const ImVec4 panel(0.025f, 0.025f, 0.025f, 1.0f);
        const ImVec4 popup(0.035f, 0.035f, 0.035f, 1.0f);
        const ImVec4 frame(0.07f, 0.07f, 0.07f, 1.0f);
        const ImVec4 hover(0.12f, 0.12f, 0.12f, 1.0f);
        const ImVec4 active(0.18f, 0.18f, 0.18f, 1.0f);
        const ImVec4 button(0.10f, 0.10f, 0.10f, 1.0f);
        const ImVec4 accent(0.42f, 0.78f, 1.0f, 1.0f);
        const ImVec4 border(0.20f, 0.20f, 0.20f, 0.72f);
        c[ImGuiCol_Text] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.48f, 0.48f, 1.0f);
        c[ImGuiCol_WindowBg] = window;
        c[ImGuiCol_ChildBg] = panel;
        c[ImGuiCol_PopupBg] = popup;
        c[ImGuiCol_Border] = border;
        c[ImGuiCol_FrameBg] = frame;
        c[ImGuiCol_FrameBgHovered] = hover;
        c[ImGuiCol_FrameBgActive] = active;
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = accent;
        c[ImGuiCol_Button] = button;
        c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive] = active;
        c[ImGuiCol_Header] = frame;
        c[ImGuiCol_HeaderHovered] = hover;
        c[ImGuiCol_HeaderActive] = active;
        c[ImGuiCol_TitleBg] = window;
        c[ImGuiCol_TitleBgActive] = button;
        c[ImGuiCol_MenuBarBg] = window;
        c[ImGuiCol_ScrollbarBg] = window;
        c[ImGuiCol_ScrollbarGrab] = button;
        c[ImGuiCol_ScrollbarGrabHovered] = hover;
        c[ImGuiCol_ScrollbarGrabActive] = active;
        c[ImGuiCol_Separator] = border;
        c[ImGuiCol_Tab] = frame;
        c[ImGuiCol_TabHovered] = hover;
        c[ImGuiCol_TabActive] = button;
        c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_TableHeaderBg] = frame;
        c[ImGuiCol_TableBorderStrong] = border;
        c[ImGuiCol_TableBorderLight] = ImVec4(border.x, border.y, border.z, 0.45f);
        c[ImGuiCol_TableRowBg] = window;
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
        c[ImGuiCol_PlotLines] = accent;
        c[ImGuiCol_PlotLinesHovered] = accent;
        c[ImGuiCol_PlotHistogram] = accent;
        c[ImGuiCol_PlotHistogramHovered] = accent;
        cfg.backgroundColor[0] = 0.0f;
        cfg.backgroundColor[1] = 0.0f;
        cfg.backgroundColor[2] = 0.0f;
        return;
    }
    if (name == "ImGui") {
        c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.93f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);
        c[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
        c[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
        c[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.16f, 0.19f, 0.96f);
        c[ImGuiCol_Border] = ImVec4(0.30f, 0.32f, 0.38f, 0.55f);
        c[ImGuiCol_FrameBg] = ImVec4(0.21f, 0.23f, 0.28f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.31f, 0.39f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.38f, 0.48f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.72f, 1.0f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.24f, 0.36f, 0.54f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.45f, 0.66f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.52f, 0.74f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.24f, 0.36f, 0.54f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.31f, 0.45f, 0.66f, 1.0f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.38f, 0.52f, 0.74f, 1.0f);
        c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.16f, 0.19f, 1.0f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.35f, 1.0f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.38f, 0.45f, 1.0f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.46f, 0.54f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(0.30f, 0.32f, 0.38f, 0.65f);
        c[ImGuiCol_Tab] = ImVec4(0.22f, 0.24f, 0.29f, 1.0f);
        c[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.35f, 0.43f, 1.0f);
        c[ImGuiCol_TabActive] = ImVec4(0.38f, 0.42f, 0.52f, 1.0f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.35f, 0.65f, 1.0f, 0.35f);
        c[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 0.65f, 0.15f, 0.90f);
        c[ImGuiCol_NavHighlight] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.30f, 0.32f, 0.38f, 0.55f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.24f, 0.26f, 0.31f, 0.45f);
        c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(0.08f, 0.09f, 0.11f, 0.35f);
        c[ImGuiCol_PlotLines] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
        c[ImGuiCol_PlotLinesHovered] = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
        c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        cfg.backgroundColor[0] = 0.10f; cfg.backgroundColor[1] = 0.10f; cfg.backgroundColor[2] = 0.10f;
        return;
    }
    if (pink || green) {
        const ImVec4 window = pink ? ImVec4(0.82f, 0.72f, 0.82f, 0.99f) : ImVec4(0.12f, 0.16f, 0.14f, 0.98f);
        const ImVec4 child = pink ? ImVec4(0.90f, 0.80f, 0.88f, 1.0f) : ImVec4(0.16f, 0.21f, 0.18f, 1.0f);
        const ImVec4 popup = pink ? ImVec4(0.86f, 0.75f, 0.84f, 0.99f) : ImVec4(0.14f, 0.19f, 0.16f, 0.98f);
        const ImVec4 frame = pink ? ImVec4(0.88f, 0.62f, 0.76f, 1.0f) : ImVec4(0.23f, 0.31f, 0.27f, 1.0f);
        const ImVec4 hover = pink ? ImVec4(0.93f, 0.70f, 0.82f, 1.0f) : ImVec4(0.31f, 0.43f, 0.35f, 1.0f);
        const ImVec4 active = pink ? ImVec4(0.84f, 0.52f, 0.69f, 1.0f) : ImVec4(0.37f, 0.52f, 0.42f, 1.0f);
        const ImVec4 button = pink ? ImVec4(0.78f, 0.46f, 0.65f, 1.0f) : ImVec4(0.42f, 0.62f, 0.50f, 1.0f);
        const ImVec4 accent = pink ? ImVec4(0.70f, 0.32f, 0.55f, 1.0f) : ImVec4(0.63f, 0.90f, 0.69f, 1.0f);
        c[ImGuiCol_Text] = pink ? ImVec4(0.28f, 0.16f, 0.25f, 1.0f) : ImVec4(0.95f, 0.92f, 0.95f, 1.0f);
        c[ImGuiCol_TextDisabled] = pink ? ImVec4(0.48f, 0.34f, 0.44f, 1.0f) : ImVec4(0.62f, 0.58f, 0.62f, 1.0f);
        c[ImGuiCol_WindowBg] = window;
        c[ImGuiCol_ChildBg] = child;
        c[ImGuiCol_PopupBg] = popup;
        c[ImGuiCol_Border] = pink ? ImVec4(1.0f, 0.38f, 0.70f, 0.62f) : ImVec4(0.39f, 0.55f, 0.45f, 0.55f);
        c[ImGuiCol_FrameBg] = frame;
        c[ImGuiCol_FrameBgHovered] = hover;
        c[ImGuiCol_FrameBgActive] = active;
        c[ImGuiCol_CheckMark] = accent;
        c[ImGuiCol_SliderGrab] = accent;
        c[ImGuiCol_SliderGrabActive] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
        c[ImGuiCol_Button] = button;
        c[ImGuiCol_ButtonHovered] = hover;
        c[ImGuiCol_ButtonActive] = active;
        c[ImGuiCol_Header] = frame;
        c[ImGuiCol_HeaderHovered] = hover;
        c[ImGuiCol_HeaderActive] = active;
        c[ImGuiCol_TitleBg] = window;
        c[ImGuiCol_TitleBgActive] = button;
        c[ImGuiCol_MenuBarBg] = popup;
        c[ImGuiCol_ScrollbarBg] = window;
        c[ImGuiCol_ScrollbarGrab] = button;
        c[ImGuiCol_ScrollbarGrabHovered] = hover;
        c[ImGuiCol_ScrollbarGrabActive] = active;
        c[ImGuiCol_Separator] = c[ImGuiCol_Border];
        c[ImGuiCol_Tab] = frame;
        c[ImGuiCol_TabHovered] = hover;
        c[ImGuiCol_TabActive] = button;
        c[ImGuiCol_TextSelectedBg] = accent; c[ImGuiCol_TextSelectedBg].w = 0.35f;
        c[ImGuiCol_DragDropTarget] = accent;
        c[ImGuiCol_NavHighlight] = accent;
        c[ImGuiCol_TableHeaderBg] = frame;
        c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_Border];
        c[ImGuiCol_TableBorderLight] = frame;
        c[ImGuiCol_TableRowBg] = ImVec4(0,0,0,0);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1,1,1,0.03f);
        c[ImGuiCol_PlotLines] = accent; c[ImGuiCol_PlotLinesHovered] = accent;
        c[ImGuiCol_PlotHistogram] = accent; c[ImGuiCol_PlotHistogramHovered] = accent;
        if (pink) { cfg.backgroundColor[0] = 0.90f; cfg.backgroundColor[1] = 0.82f; cfg.backgroundColor[2] = 0.88f; }
        else { cfg.backgroundColor[0] = 0.07f; cfg.backgroundColor[1] = 0.11f; cfg.backgroundColor[2] = 0.09f; }
        return;
    }
    c[ImGuiCol_Text] = ImVec4(cfg.customThemeText[0], cfg.customThemeText[1], cfg.customThemeText[2], cfg.customThemeText[3]);
    c[ImGuiCol_TextDisabled] = ImVec4(cfg.customThemeTextDisabled[0], cfg.customThemeTextDisabled[1], cfg.customThemeTextDisabled[2], cfg.customThemeTextDisabled[3]);
    c[ImGuiCol_WindowBg] = ImVec4(cfg.customThemeWindowBg[0], cfg.customThemeWindowBg[1], cfg.customThemeWindowBg[2], 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(cfg.customThemeChildBg[0], cfg.customThemeChildBg[1], cfg.customThemeChildBg[2], 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(cfg.customThemePopupBg[0], cfg.customThemePopupBg[1], cfg.customThemePopupBg[2], cfg.customThemePopupBg[3]);
    c[ImGuiCol_Border] = ImVec4(cfg.customThemeBorder[0], cfg.customThemeBorder[1], cfg.customThemeBorder[2], cfg.customThemeBorder[3]);
    c[ImGuiCol_BorderShadow] = ImVec4(cfg.customThemeBorder[0], cfg.customThemeBorder[1], cfg.customThemeBorder[2], cfg.customThemeBorder[3] * 0.45f);
    c[ImGuiCol_FrameBg] = ImVec4(cfg.customThemeFrameBg[0], cfg.customThemeFrameBg[1], cfg.customThemeFrameBg[2], cfg.customThemeFrameBg[3]);
    c[ImGuiCol_FrameBgHovered] = ImVec4(cfg.customThemeFrameHovered[0], cfg.customThemeFrameHovered[1], cfg.customThemeFrameHovered[2], cfg.customThemeFrameHovered[3]);
    c[ImGuiCol_FrameBgActive] = ImVec4(cfg.customThemeFrameActive[0], cfg.customThemeFrameActive[1], cfg.customThemeFrameActive[2], cfg.customThemeFrameActive[3]);
    c[ImGuiCol_Button] = ImVec4(cfg.customThemeButton[0], cfg.customThemeButton[1], cfg.customThemeButton[2], cfg.customThemeButton[3]);
    c[ImGuiCol_CheckMark] = ImVec4(cfg.customThemeAccent[0], cfg.customThemeAccent[1], cfg.customThemeAccent[2], cfg.customThemeAccent[3]);
    c[ImGuiCol_SliderGrab] = ImVec4(cfg.customThemeAccent[0], cfg.customThemeAccent[1], cfg.customThemeAccent[2], cfg.customThemeAccent[3]);
    c[ImGuiCol_SliderGrabActive] = ImVec4(cfg.customThemeAccent[0], cfg.customThemeAccent[1], cfg.customThemeAccent[2], cfg.customThemeAccent[3]);
    c[ImGuiCol_ButtonHovered] = ImVec4(cfg.customThemeHeaderHovered[0], cfg.customThemeHeaderHovered[1], cfg.customThemeHeaderHovered[2], cfg.customThemeHeaderHovered[3]);
    c[ImGuiCol_ButtonActive] = ImVec4(cfg.customThemeHeaderActive[0], cfg.customThemeHeaderActive[1], cfg.customThemeHeaderActive[2], cfg.customThemeHeaderActive[3]);
    c[ImGuiCol_TitleBg] = ImVec4(cfg.customThemeTitleBg[0], cfg.customThemeTitleBg[1], cfg.customThemeTitleBg[2], cfg.customThemeTitleBg[3]);
    c[ImGuiCol_TitleBgActive] = ImVec4(cfg.customThemeTitleBgActive[0], cfg.customThemeTitleBgActive[1], cfg.customThemeTitleBgActive[2], cfg.customThemeTitleBgActive[3]);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(cfg.customThemeTitleBg[0], cfg.customThemeTitleBg[1], cfg.customThemeTitleBg[2], cfg.customThemeTitleBg[3]);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(cfg.customThemePopupBg[0], cfg.customThemePopupBg[1], cfg.customThemePopupBg[2], 0.55f);
    c[ImGuiCol_MenuBarBg] = ImVec4(cfg.customThemeMenuBarBg[0], cfg.customThemeMenuBarBg[1], cfg.customThemeMenuBarBg[2], cfg.customThemeMenuBarBg[3]);
    c[ImGuiCol_ScrollbarBg] = ImVec4(cfg.customThemeScrollbarBg[0], cfg.customThemeScrollbarBg[1], cfg.customThemeScrollbarBg[2], cfg.customThemeScrollbarBg[3]);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(cfg.customThemeScrollbarGrab[0], cfg.customThemeScrollbarGrab[1], cfg.customThemeScrollbarGrab[2], cfg.customThemeScrollbarGrab[3]);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(cfg.customThemeHeaderHovered[0], cfg.customThemeHeaderHovered[1], cfg.customThemeHeaderHovered[2], cfg.customThemeHeaderHovered[3]);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(cfg.customThemeHeaderActive[0], cfg.customThemeHeaderActive[1], cfg.customThemeHeaderActive[2], cfg.customThemeHeaderActive[3]);
    c[ImGuiCol_Header] = ImVec4(cfg.customThemeHeader[0], cfg.customThemeHeader[1], cfg.customThemeHeader[2], cfg.customThemeHeader[3]);
    c[ImGuiCol_HeaderHovered] = ImVec4(cfg.customThemeHeaderHovered[0], cfg.customThemeHeaderHovered[1], cfg.customThemeHeaderHovered[2], cfg.customThemeHeaderHovered[3]);
    c[ImGuiCol_HeaderActive] = ImVec4(cfg.customThemeHeaderActive[0], cfg.customThemeHeaderActive[1], cfg.customThemeHeaderActive[2], cfg.customThemeHeaderActive[3]);
    c[ImGuiCol_Separator] = ImVec4(cfg.customThemeSeparator[0], cfg.customThemeSeparator[1], cfg.customThemeSeparator[2], cfg.customThemeSeparator[3]);
    c[ImGuiCol_SeparatorHovered] = ImVec4(cfg.customThemeHeaderHovered[0], cfg.customThemeHeaderHovered[1], cfg.customThemeHeaderHovered[2], cfg.customThemeHeaderHovered[3]);
    c[ImGuiCol_SeparatorActive] = ImVec4(cfg.customThemeHeaderActive[0], cfg.customThemeHeaderActive[1], cfg.customThemeHeaderActive[2], cfg.customThemeHeaderActive[3]);
    c[ImGuiCol_ResizeGrip] = ImVec4(cfg.customThemeResizeGrip[0], cfg.customThemeResizeGrip[1], cfg.customThemeResizeGrip[2], cfg.customThemeResizeGrip[3]);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(cfg.customThemeHeaderHovered[0], cfg.customThemeHeaderHovered[1], cfg.customThemeHeaderHovered[2], cfg.customThemeHeaderHovered[3]);
    c[ImGuiCol_ResizeGripActive] = ImVec4(cfg.customThemeHeaderActive[0], cfg.customThemeHeaderActive[1], cfg.customThemeHeaderActive[2], cfg.customThemeHeaderActive[3]);
    c[ImGuiCol_Tab] = ImVec4(cfg.customThemeTab[0], cfg.customThemeTab[1], cfg.customThemeTab[2], cfg.customThemeTab[3]);
    c[ImGuiCol_TabHovered] = ImVec4(cfg.customThemeTabHovered[0], cfg.customThemeTabHovered[1], cfg.customThemeTabHovered[2], cfg.customThemeTabHovered[3]);
    c[ImGuiCol_TabActive] = ImVec4(cfg.customThemeTabActive[0], cfg.customThemeTabActive[1], cfg.customThemeTabActive[2], cfg.customThemeTabActive[3]);
    c[ImGuiCol_TextSelectedBg] = ImVec4(cfg.customThemeTextSelected[0], cfg.customThemeTextSelected[1], cfg.customThemeTextSelected[2], cfg.customThemeTextSelected[3]);
    c[ImGuiCol_DragDropTarget] = ImVec4(cfg.customThemeDragDrop[0], cfg.customThemeDragDrop[1], cfg.customThemeDragDrop[2], cfg.customThemeDragDrop[3]);
    c[ImGuiCol_NavHighlight] = ImVec4(cfg.customThemeNav[0], cfg.customThemeNav[1], cfg.customThemeNav[2], cfg.customThemeNav[3]);
    c[ImGuiCol_TableHeaderBg] = ImVec4(cfg.customThemeTableHeader[0], cfg.customThemeTableHeader[1], cfg.customThemeTableHeader[2], cfg.customThemeTableHeader[3]);
    c[ImGuiCol_TableBorderStrong] = ImVec4(cfg.customThemeTableBorder[0], cfg.customThemeTableBorder[1], cfg.customThemeTableBorder[2], cfg.customThemeTableBorder[3]);
    c[ImGuiCol_TableBorderLight] = ImVec4(cfg.customThemeTableBorder[0], cfg.customThemeTableBorder[1], cfg.customThemeTableBorder[2], cfg.customThemeTableBorder[3] * 0.75f);
    c[ImGuiCol_TableRowBg] = ImVec4(cfg.customThemeTableRow[0], cfg.customThemeTableRow[1], cfg.customThemeTableRow[2], cfg.customThemeTableRow[3]);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(cfg.customThemeTableRowAlt[0], cfg.customThemeTableRowAlt[1], cfg.customThemeTableRowAlt[2], cfg.customThemeTableRowAlt[3]);
    c[ImGuiCol_PlotLines] = ImVec4(cfg.customThemePlot[0], cfg.customThemePlot[1], cfg.customThemePlot[2], cfg.customThemePlot[3]);
    c[ImGuiCol_PlotLinesHovered] = ImVec4(cfg.customThemePlotHovered[0], cfg.customThemePlotHovered[1], cfg.customThemePlotHovered[2], cfg.customThemePlotHovered[3]);
    c[ImGuiCol_PlotHistogram] = ImVec4(cfg.customThemePlot[0], cfg.customThemePlot[1], cfg.customThemePlot[2], cfg.customThemePlot[3]);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(cfg.customThemePlotHovered[0], cfg.customThemePlotHovered[1], cfg.customThemePlotHovered[2], cfg.customThemePlotHovered[3]);
    cfg.backgroundColor[0] = cfg.customThemeViewportBg[0];
    cfg.backgroundColor[1] = cfg.customThemeViewportBg[1];
    cfg.backgroundColor[2] = cfg.customThemeViewportBg[2];
}

bool IsPresetTheme(const std::string& name) {
    return name == "ImGui" || name == "Pastel Pink" || name == "Pastel Green" || name == "AMOLED";
}

void ApplyTheme(EditorConfig& cfg) {
    SetTheme(cfg, IsPresetTheme(cfg.themeName) ? cfg.themeName : "Custom");
}

void CaptureTheme(EditorConfig& cfg) {
    const ImVec4* c = ImGui::GetStyle().Colors;
    auto copy = [](float* dst, const ImVec4& src) {
        dst[0] = src.x;
        dst[1] = src.y;
        dst[2] = src.z;
        dst[3] = src.w;
    };
    copy(cfg.customThemeWindowBg, c[ImGuiCol_WindowBg]);
    copy(cfg.customThemeChildBg, c[ImGuiCol_ChildBg]);
    copy(cfg.customThemePopupBg, c[ImGuiCol_PopupBg]);
    copy(cfg.customThemeFrameBg, c[ImGuiCol_FrameBg]);
    copy(cfg.customThemeFrameHovered, c[ImGuiCol_FrameBgHovered]);
    copy(cfg.customThemeFrameActive, c[ImGuiCol_FrameBgActive]);
    copy(cfg.customThemeButton, c[ImGuiCol_Button]);
    copy(cfg.customThemeText, c[ImGuiCol_Text]);
    copy(cfg.customThemeTextDisabled, c[ImGuiCol_TextDisabled]);
    copy(cfg.customThemeAccent, c[ImGuiCol_CheckMark]);
    copy(cfg.customThemeBorder, c[ImGuiCol_Border]);
    copy(cfg.customThemeTitleBg, c[ImGuiCol_TitleBg]);
    copy(cfg.customThemeTitleBgActive, c[ImGuiCol_TitleBgActive]);
    copy(cfg.customThemeMenuBarBg, c[ImGuiCol_MenuBarBg]);
    copy(cfg.customThemeScrollbarBg, c[ImGuiCol_ScrollbarBg]);
    copy(cfg.customThemeScrollbarGrab, c[ImGuiCol_ScrollbarGrab]);
    copy(cfg.customThemeHeader, c[ImGuiCol_Header]);
    copy(cfg.customThemeHeaderHovered, c[ImGuiCol_HeaderHovered]);
    copy(cfg.customThemeHeaderActive, c[ImGuiCol_HeaderActive]);
    copy(cfg.customThemeSeparator, c[ImGuiCol_Separator]);
    copy(cfg.customThemeResizeGrip, c[ImGuiCol_ResizeGrip]);
    copy(cfg.customThemeTab, c[ImGuiCol_Tab]);
    copy(cfg.customThemeTabHovered, c[ImGuiCol_TabHovered]);
    copy(cfg.customThemeTabActive, c[ImGuiCol_TabActive]);
    copy(cfg.customThemeTextSelected, c[ImGuiCol_TextSelectedBg]);
    copy(cfg.customThemeDragDrop, c[ImGuiCol_DragDropTarget]);
    copy(cfg.customThemeNav, c[ImGuiCol_NavHighlight]);
    copy(cfg.customThemeTableHeader, c[ImGuiCol_TableHeaderBg]);
    copy(cfg.customThemeTableBorder, c[ImGuiCol_TableBorderStrong]);
    copy(cfg.customThemeTableRow, c[ImGuiCol_TableRowBg]);
    copy(cfg.customThemeTableRowAlt, c[ImGuiCol_TableRowBgAlt]);
    copy(cfg.customThemePlot, c[ImGuiCol_PlotLines]);
    copy(cfg.customThemePlotHovered, c[ImGuiCol_PlotLinesHovered]);
    cfg.customThemeViewportBg[0] = cfg.backgroundColor[0];
    cfg.customThemeViewportBg[1] = cfg.backgroundColor[1];
    cfg.customThemeViewportBg[2] = cfg.backgroundColor[2];
}

bool DrawCheckbox(const char* label, bool* value) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 frame = style.Colors[ImGuiCol_Button];
    const ImVec4 hovered = style.Colors[ImGuiCol_ButtonHovered];
    const ImVec4 active = style.Colors[ImGuiCol_ButtonActive];
    const ImVec4 mark = style.Colors[ImGuiCol_CheckMark];

    ImGui::PushID(label);
    const float square = ImGui::GetFrameHeight();
    const char* displayLabel = Tr(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(displayLabel);
    const ImVec2 totalSize(square + style.ItemInnerSpacing.x + labelSize.x, square);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(Tr("##theme_checkbox"), totalSize);
    const bool hoveredItem = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (pressed) *value = !*value;

    const ImVec4& fill = held ? active : (hoveredItem ? hovered : frame);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 boxMax(pos.x + square, pos.y + square);
    drawList->AddRectFilled(pos, boxMax, ImGui::ColorConvertFloat4ToU32(fill), style.FrameRounding);
    if (*value) {
        const float pad = std::max(1.0f, square * 0.22f);
        const ImVec2 check[] = {
            ImVec2(pos.x + pad, pos.y + square * 0.50f),
            ImVec2(pos.x + square * 0.43f, pos.y + square - pad),
            ImVec2(pos.x + square - pad, pos.y + pad)
        };
        const ImU32 markColor = ImGui::ColorConvertFloat4ToU32(mark);
        drawList->AddPolyline(check, 3, markColor, ImDrawFlags_None, std::max(1.5f, square * 0.13f));
    }
    const ImVec2 textPos(pos.x + square + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y);
    drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]), displayLabel);
    ImGui::PopID();
    return pressed;
}


bool CreatorGridBegin(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::PopStyleVar(2);
        return false;
    }
    ImGui::TableSetupColumn(Tr("##Setting"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn(Tr("##Value"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
    return true;
}

struct CreatorPanelsLayout {
    bool stacked;
    float previewWidth;
    float previewHeight;
};

CreatorPanelsLayout GetCreatorPanelsLayout() {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const bool stacked = available.x < 900.0f;
    return {
        stacked,
        stacked ? 0.0f : std::clamp(available.x * 0.44f, 320.0f, 520.0f),
        stacked ? std::clamp(available.y * 0.36f, 190.0f, 280.0f) : 0.0f
    };
}

void CreatorGridSection(const char* title) {
    const ImVec2 cellPadding = ImGui::GetStyle().CellPadding;
    const float rowHeight = ImGui::GetFrameHeight() + cellPadding.y * 2.0f;
    const float titleInset = 8.0f;
    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
    ImGui::TableSetColumnIndex(0);
    const ImVec2 firstCellPos = ImGui::GetCursorScreenPos();
    const float rounding = ImGui::GetStyle().FrameRounding;
    ImGui::TableSetColumnIndex(1);
    const ImVec2 secondCellPos = ImGui::GetCursorScreenPos();
    const float secondCellWidth = ImGui::GetContentRegionAvail().x;
    ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
    ImVec2 clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
    clipMin.x = firstCellPos.x - cellPadding.x;
    clipMax.x = secondCellPos.x + secondCellWidth + cellPadding.x;
    ImGui::GetWindowDrawList()->PushClipRect(clipMin, clipMax, false);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(clipMin.x, firstCellPos.y + cellPadding.y),
        ImVec2(clipMax.x, secondCellPos.y + rowHeight - cellPadding.y),
        ImGui::GetColorU32(ImGuiCol_Header), rounding);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(firstCellPos.x + titleInset, firstCellPos.y + cellPadding.y + ImGui::GetStyle().FramePadding.y),
        IM_COL32(255, 255, 255, 255), Tr(title));
    ImGui::GetWindowDrawList()->PopClipRect();
}

bool CreatorGridSlider(const char* label, float* value, float minValue, float maxValue, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", Tr(label));
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1.0f);
    const bool changed = ImGui::SliderFloat(Tr("##value"), value, minValue, maxValue, "%.2f");
    if (tooltip && tooltip[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(tooltip));
    ImGui::PopID();
    return changed;
}

bool CreatorGridCheckbox(const char* label, bool* value, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", Tr(label));
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
    const bool changed = DrawCheckbox(Tr(""), value);
    if (tooltip && tooltip[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(tooltip));
    ImGui::PopID();
    return changed;
}

bool CreatorGridCombo(const char* label, int* value, const char* const items[], int itemCount, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", Tr(label));
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1.0f);
    std::vector<const char*> localizedItems;
    localizedItems.reserve(static_cast<std::size_t>(itemCount));
    for (int i = 0; i < itemCount; ++i) localizedItems.push_back(Tr(items[i]));
    const bool changed = ImGui::Combo(Tr("##value"), value, localizedItems.data(), itemCount);
    if (tooltip && tooltip[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(tooltip));
    ImGui::PopID();
    return changed;
}

void CreatorGridEnd() {
    ImGui::EndTable();
    ImGui::PopStyleVar(2);
}

void DrawCreatorImagePreview(const char* title, PreviewState& preview) {
    ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr(title));
    ImGui::SameLine();
    if (ImGui::Button(Tr("SWAP"))) preview.showSource = !preview.showSource;
    ImGui::SameLine();
    if (ImGui::Button(Tr("FIT"))) { preview.zoom = 1.0f; preview.pan = ImVec2(0.0f, 0.0f); }
    ImGui::SameLine();
    ImGui::Text(Tr("%.0f%%"), preview.zoom * 100.0f);
    ImGui::Spacing();

    const TexturePreviewInfo* info = &preview.info;
    if (preview.showSource && g_creatorSourcePreview.info.valid) info = &g_creatorSourcePreview.info;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (!info->valid || info->texture == 0 || info->width <= 0 || info->height <= 0) {
        ImGui::SetCursorPos(ImVec2(std::max(8.0f, (avail.x - 170.0f) * 0.5f), std::max(20.0f, (avail.y - 20.0f) * 0.5f)));
        ImGui::TextDisabled(Tr("PREVIEW UNAVAILABLE"));
        return;
    }

    ImGui::BeginChild(Tr("##CreatorPreviewCanvas"), ImVec2(0.0f, std::max(40.0f, avail.y)), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    const float aspect = static_cast<float>(info->width) / static_cast<float>(std::max(1, info->height));
    float fitW = canvas.x - 12.0f;
    float fitH = canvas.y - 12.0f;
    if (fitW / std::max(0.01f, aspect) > fitH) fitW = fitH * aspect;
    else fitH = fitW / std::max(0.01f, aspect);
    const float imageW = std::max(8.0f, fitW * preview.zoom);
    const float imageH = std::max(8.0f, fitH * preview.zoom);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + canvas.x * 0.5f + preview.pan.x, origin.y + canvas.y * 0.5f + preview.pan.y);
    const ImVec2 minPos(center.x - imageW * 0.5f, center.y - imageH * 0.5f);

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(Tr("##CreatorPreviewInput"), canvas, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const float oldZoom = preview.zoom;
            preview.zoom = std::clamp(preview.zoom * std::pow(1.15f, wheel), 0.1f, 8.0f);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 rel(mouse.x - (origin.x + canvas.x * 0.5f), mouse.y - (origin.y + canvas.y * 0.5f));
            const float ratio = preview.zoom / oldZoom - 1.0f;
            preview.pan.x -= rel.x * ratio;
            preview.pan.y -= rel.y * ratio;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            preview.pan.x += delta.x;
            preview.pan.y += delta.y;
        }
    }
    ImGui::SetCursorScreenPos(minPos);
    ImGui::Image(static_cast<ImTextureID>(info->texture), ImVec2(imageW, imageH));
    ImGui::EndChild();
}

void LeaveMaterialCreator() {
    ReleaseCreatorPreview(g_creatorNormalPreview);
    ReleaseCreatorPreview(g_creatorGlossPreview);
    ReleaseCreatorPreview(g_creatorBumpPreview);
    ReleaseCreatorPreview(g_creatorSourcePreview);
    g_materialCreatorDiffuse[0] = '\0';
    g_creatorSaveKind = CreatorSaveKind::None;
    g_materialCreator = false;
    g_showBrowser = true;
    g_showTexturePreview = true;
    g_showMaterialEditor = true;
}

std::string CreatorBaseStem() {
    std::string selected = g_materialCreatorDiffuse;
    if (selected.empty()) return "new_texture";
    if (selected.rfind("wad://", 0) == 0 || selected.rfind("wad:/", 0) == 0) {
        const std::size_t separator = selected.rfind('#');
        if (separator != std::string::npos) selected = selected.substr(separator + 1);
    }
    std::string stem = fs::path(selected).stem().string();
    while (true) {
        const std::string lower = ToLower(stem);
        if (lower.size() > 5 && lower.compare(lower.size() - 5, 5, "_norm") == 0) {
            stem.resize(stem.size() - 5);
            continue;
        }
        if (lower.size() > 6 && lower.compare(lower.size() - 6, 6, "_gloss") == 0) {
            stem.resize(stem.size() - 6);
            continue;
        }
        if (lower.size() > 5 && lower.compare(lower.size() - 5, 5, "_hmap") == 0) {
            stem.resize(stem.size() - 5);
            continue;
        }
        break;
    }
    return stem.empty() ? "new_texture" : stem;
}

void ReleaseCreatorPreview(PreviewState& preview) {
    ReleaseTexturePreview(preview.info);
    preview.reference.clear();
    preview.kind.clear();
}

void SetCreatorDiffuse(const std::string& reference) {
    if (reference.empty()) return;
    std::snprintf(g_materialCreatorDiffuse, sizeof(g_materialCreatorDiffuse), "%s", reference.c_str());
    g_materialCreatorDiffuse[sizeof(g_materialCreatorDiffuse) - 1] = '\0';
    ReleaseCreatorPreview(g_creatorSourcePreview);
    g_creatorSourcePreview.info = LoadTexturePreview(g_materialCreatorDiffuse);
    g_creatorSourcePreview.reference = g_materialCreatorDiffuse;
    g_creatorSourcePreview.kind = "SOURCE";
    g_creatorSourcePreview.zoom = 1.0f;
    g_creatorSourcePreview.pan = ImVec2(0.0f, 0.0f);
    const std::string stem = CreatorBaseStem();
    std::snprintf(g_materialCreatorNormalName, sizeof(g_materialCreatorNormalName), "%s_norm.dds", stem.c_str());
    std::snprintf(g_materialCreatorGlossName, sizeof(g_materialCreatorGlossName), "%s_gloss.dds", stem.c_str());
    std::snprintf(g_materialCreatorBumpName, sizeof(g_materialCreatorBumpName), "%s.dds", stem.c_str());
    std::string folder = CreatorSelectedTextureFolder();
    if (!folder.empty()) {
        std::snprintf(g_materialCreatorOutputFolder, sizeof(g_materialCreatorOutputFolder), "%s", folder.c_str());
        g_materialCreatorOutputFolder[sizeof(g_materialCreatorOutputFolder) - 1] = '\0';
    }
}

NormalMapSettings GetCreatorNormalSettings() {
    NormalMapSettings settings;
    settings.strength = g_materialCreatorNormalStrength;
    settings.flipX = g_materialCreatorFlipX;
    settings.flipY = g_materialCreatorFlipY;
    settings.fullZRange = g_materialCreatorFullZRange;
    settings.heightChannel = g_materialCreatorNormalHeightChannel;
    settings.invertHeight = g_materialCreatorNormalInvertHeight;
    settings.sharpness = g_materialCreatorNormalSharpness;
    settings.mipmaps = g_materialCreatorNormalMipmaps;
    settings.blackPoint = g_materialCreatorNormalBlackPoint;
    settings.whitePoint = g_materialCreatorNormalWhitePoint;
    settings.smoothing = g_materialCreatorNormalSmoothing;
    settings.gradientFilter = g_materialCreatorNormalFilter;
    settings.tileEdges = g_materialCreatorNormalTileEdges;
    return settings;
}

GlossMapSettings GetCreatorGlossSettings() {
    GlossMapSettings settings;
    settings.contrast = g_materialCreatorGlossContrast;
    settings.brightness = g_materialCreatorGlossBrightness;
    settings.power = g_materialCreatorGlossPower;
    settings.invert = g_materialCreatorGlossInvert;
    settings.lowerThreshold = g_materialCreatorGlossLower;
    settings.upperThreshold = g_materialCreatorGlossUpper;
    settings.normalize = g_materialCreatorGlossNormalize;
    settings.sharpness = g_materialCreatorGlossSharpness;
    settings.mipmaps = g_materialCreatorGlossMipmaps;
    settings.sourceMode = g_materialCreatorGlossSource;
    settings.softness = g_materialCreatorGlossSoftness;
    return settings;
}

BumpMapSettings GetCreatorBumpSettings() {
    BumpMapSettings settings;
    settings.heightChannel = g_creatorBumpHeightChannel;
    settings.invert = g_creatorBumpInvert;
    settings.contrast = g_creatorBumpContrast;
    settings.brightness = g_creatorBumpBrightness;
    settings.normalize = g_creatorBumpNormalize;
    settings.sharpness = g_creatorBumpSharpness;
    settings.mipmaps = g_creatorBumpMipmaps;
    settings.blackPoint = g_creatorBumpBlackPoint;
    settings.whitePoint = g_creatorBumpWhitePoint;
    settings.gamma = g_creatorBumpGamma;
    settings.smoothing = g_creatorBumpSmoothing;
    settings.tileEdges = g_creatorBumpTileEdges;
    return settings;
}

void GenerateCreatorNormalPreview() {
    if (g_materialCreatorDiffuse[0] == '\0') return;
    ReleaseCreatorPreview(g_creatorNormalPreview);
    TexturePreviewInfo next = GenerateNormalMapPreviewTexture(g_materialCreatorDiffuse, GetCreatorNormalSettings());
    if (!next.valid || next.texture == 0) {
        ReleaseTexturePreview(next);
        return;
    }
    g_creatorNormalPreview.info = next;
    g_creatorNormalPreview.reference = "__creator_normal_preview";
    g_creatorNormalPreview.kind = "NORMAL";
}

void GenerateCreatorGlossPreview() {
    if (g_materialCreatorDiffuse[0] == '\0') return;
    ReleaseCreatorPreview(g_creatorGlossPreview);
    TexturePreviewInfo next = GenerateGlossMapPreviewTexture(g_materialCreatorDiffuse, GetCreatorGlossSettings());
    if (!next.valid || next.texture == 0) {
        ReleaseTexturePreview(next);
        return;
    }
    g_creatorGlossPreview.info = next;
    g_creatorGlossPreview.reference = "__creator_gloss_preview";
    g_creatorGlossPreview.kind = "GLOSS";
}


void GenerateCreatorBumpPreview() {
    if (g_materialCreatorDiffuse[0] == '\0') return;
    ReleaseCreatorPreview(g_creatorBumpPreview);
    TexturePreviewInfo next = GenerateBumpMapPreviewTexture(g_materialCreatorDiffuse, GetCreatorBumpSettings());
    if (next.valid && next.texture != 0) g_creatorBumpPreview.info = next;
}

void CreatorNavigateSaveFolder(const fs::path& folder) {
    std::error_code ec;
    const fs::path normalized = fs::weakly_canonical(folder, ec);
    if (ec || !fs::is_directory(normalized, ec) || !CreatorPathInsideGame(normalized)) return;
    if (!g_creatorSaveHistory.empty() && g_creatorSaveHistory[g_creatorSaveHistoryIndex] == normalized) return;
    if (!g_creatorSaveHistory.empty() && g_creatorSaveHistoryIndex + 1 < g_creatorSaveHistory.size()) {
        g_creatorSaveHistory.erase(g_creatorSaveHistory.begin() + static_cast<std::ptrdiff_t>(g_creatorSaveHistoryIndex + 1), g_creatorSaveHistory.end());
    }
    g_creatorSaveHistory.push_back(normalized);
    g_creatorSaveHistoryIndex = g_creatorSaveHistory.size() - 1;
    g_creatorFolderBrowsePath = normalized;
    const std::string relative = CreatorRelativePath(normalized);
    std::snprintf(g_materialCreatorOutputFolder, sizeof(g_materialCreatorOutputFolder), "%s", relative.c_str());
    g_materialCreatorOutputFolder[sizeof(g_materialCreatorOutputFolder) - 1] = '\0';
}

void RequestCreatorSave(CreatorSaveKind kind) {
    if (g_materialCreatorDiffuse[0] == '\0') return;
    g_creatorSaveKind = kind;
    if (kind == CreatorSaveKind::Normal) {
        g_creatorSaveFormat = g_materialCreatorNormalFormat == 0 ? 1 : 2;
    } else if (kind == CreatorSaveKind::Gloss) {
        g_creatorSaveFormat = g_materialCreatorGlossFormat == 0 ? 0 : 2;
    } else {
        g_creatorSaveFormat = g_creatorBumpFormat == 0 ? 0 : 2;
    }

    const std::string stem = CreatorBaseStem();
    const char* suffix = kind == CreatorSaveKind::Gloss ? "gloss" : "norm";
    if (kind == CreatorSaveKind::Bump) std::snprintf(g_creatorSaveName, sizeof(g_creatorSaveName), "%s_hmap.dds", stem.c_str());
    else std::snprintf(g_creatorSaveName, sizeof(g_creatorSaveName), "%s_%s.dds", stem.c_str(), suffix);
    fs::path start = gameRootPath;
    if (g_materialCreatorOutputFolder[0]) {
        fs::path candidate = gameRootPath / g_materialCreatorOutputFolder;
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && CreatorPathInsideGame(candidate)) start = candidate;
    }
    g_creatorSaveHistory.clear();
    g_creatorSaveHistoryIndex = 0;
    g_creatorFolderBrowsePath.clear();
    CreatorNavigateSaveFolder(start);
    ImGui::OpenPopup(Tr("##CreatorSaveTexture"));
}

bool SaveCreatorTextureNow(CreatorSaveKind kind, const fs::path& output, int format) {
    if (kind == CreatorSaveKind::None || g_materialCreatorDiffuse[0] == '\0') return false;
    if (!CreatorPathInsideGame(output)) return false;
    fs::path temp = output;
    temp += ".matedit_tmp";
    std::error_code ec;
    fs::remove(temp, ec);
    bool generated = false;
    if (kind == CreatorSaveKind::Normal) {
        generated = GenerateNormalMapDDS(g_materialCreatorDiffuse, temp.string(), GetCreatorNormalSettings(), format);
    } else if (kind == CreatorSaveKind::Gloss) {
        generated = GenerateGlossMapDDS(g_materialCreatorDiffuse, temp.string(), GetCreatorGlossSettings(), format);
    } else {
        generated = GenerateBumpMapDDS(g_materialCreatorDiffuse, temp.string(), GetCreatorBumpSettings(), format);
    }
    if (!generated) { fs::remove(temp, ec); return false; }
    fs::remove(output, ec);
    ec.clear();
    fs::rename(temp, output, ec);
    if (ec) { fs::remove(temp, ec); return false; }
    return true;
}

void DrawCreatorReplacePopup() {
    if (g_creatorOpenReplacePopup) {
        g_creatorOpenReplacePopup = false;
        ImGui::OpenPopup(Tr("##CreatorReplaceTexture"));
    }
    static ImVec2 lastViewportSize(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    if (viewportSize.x != lastViewportSize.x || viewportSize.y != lastViewportSize.y) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        lastViewportSize = viewportSize;
    }
    if (!BeginSpacedModal("##CreatorReplaceTexture", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    ImGui::Text(Tr("File already exists."));
    ImGui::TextWrapped(Tr("%s"), g_creatorPendingReplacePath.generic_string().c_str());
    ImGui::Spacing();
    if (ImGui::Button(Tr("REPLACE"), ImVec2(140.0f, 32.0f))) {
        if (SaveCreatorTextureNow(g_creatorPendingReplaceKind, g_creatorPendingReplacePath, g_creatorPendingReplaceFormat)) {
            ReleasePreviewForPath(g_creatorPendingReplacePath);
            const std::string savedName = g_creatorPendingReplacePath.filename().string();
            if (g_creatorPendingReplaceKind == CreatorSaveKind::Normal) {
                std::snprintf(g_materialCreatorNormalName, sizeof(g_materialCreatorNormalName), "%s", savedName.c_str());
                g_materialCreatorNormalFormat = g_creatorPendingReplaceFormat == 2 ? 1 : 0;
            } else if (g_creatorPendingReplaceKind == CreatorSaveKind::Gloss) {
                std::snprintf(g_materialCreatorGlossName, sizeof(g_materialCreatorGlossName), "%s", savedName.c_str());
                g_materialCreatorGlossFormat = g_creatorPendingReplaceFormat == 2 ? 1 : 0;
            } else if (g_creatorPendingReplaceKind == CreatorSaveKind::Bump) {
                std::snprintf(g_materialCreatorBumpName, sizeof(g_materialCreatorBumpName), "%s", savedName.c_str());
                g_creatorBumpFormat = g_creatorPendingReplaceFormat == 2 ? 1 : 0;
            }
            InvalidateBrowserCaches();
            g_creatorSaveKind = CreatorSaveKind::None;
            g_creatorPendingReplaceKind = CreatorSaveKind::None;
            g_creatorPendingReplaceFormat = 0;
            g_creatorPendingReplacePath.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("CANCEL"), ImVec2(110.0f, 32.0f))) {
        g_creatorPendingReplaceKind = CreatorSaveKind::None;
        g_creatorPendingReplaceFormat = 0;
        g_creatorPendingReplacePath.clear();
        g_creatorOpenReplacePopup = false;
        ImGui::CloseCurrentPopup();
    }
    EndSpacedModal();
}

void DrawCreatorSavePopup() {
    static ImVec2 lastViewportSize(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 viewportCenter = ImGui::GetMainViewport()->GetCenter();
    if (viewportSize.x != lastViewportSize.x || viewportSize.y != lastViewportSize.y) {
        ImGui::SetNextWindowPos(viewportCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        lastViewportSize = viewportSize;
    }
    if (!ImGui::BeginPopupModal(Tr("##CreatorSaveTexture"), nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) return;
    ImGui::SetWindowSize(ImVec2(std::clamp(viewportSize.x - 40.0f, 720.0f, 1000.0f), std::clamp(viewportSize.y - 40.0f, 460.0f, 680.0f)), ImGuiCond_Always);
    const ImVec2 content = ImGui::GetContentRegionAvail();
    const float topHeight = 48.0f;

    ImGui::BeginChild(Tr("##CreatorSaveTop"), ImVec2(0.0f, topHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
    const float topPad = 8.0f;
    const float cancelWidth = 86.0f;
    const float saveWidth = 86.0f;
    ImGui::SetCursorPosX(topPad);
    if (ImGui::Button(Tr("CANCEL"), ImVec2(cancelWidth, 34.0f))) {
        g_creatorSaveKind = CreatorSaveKind::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const float titleWidth = ImGui::CalcTextSize(Tr("SAVE TEXTURE")).x;
    ImGui::SetCursorPosX(std::max(topPad + cancelWidth + 8.0f, (ImGui::GetWindowWidth() - titleWidth) * 0.5f));
    ImGui::TextUnformatted(Tr("SAVE TEXTURE"));
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - topPad - saveWidth);
    const bool savePressed = ImGui::Button(Tr("SAVE"), ImVec2(saveWidth, 34.0f));
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::Separator();

    const float bottomHeight = 58.0f;
    const float bodyHeight = std::max(220.0f, content.y - topHeight - bottomHeight - 2.0f);
    const float sideWidth = std::clamp(ImGui::GetWindowWidth() * 0.20f, 160.0f, 210.0f);

    ImGui::BeginChild(Tr("##CreatorSaveBody"), ImVec2(0.0f, bodyHeight), false);
    ImGui::BeginChild(Tr("##CreatorSaveSidebar"), ImVec2(sideWidth, 0.0f), true);
    ImGui::TextDisabled(Tr("PLACES"));
    ImGui::Spacing();
    if (ImGui::Selectable(Tr("GAME ROOT"), g_creatorFolderBrowsePath == gameRootPath)) CreatorNavigateSaveFolder(gameRootPath);
    if (ImGui::Selectable(Tr("TEXTURES"), false)) CreatorNavigateSaveFolder(gameRootPath / "textures");
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild(Tr("##CreatorSaveFiles"), ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::BeginChild(Tr("##CreatorSaveLocation"), ImVec2(0.0f, 40.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool canBack = !g_creatorSaveHistory.empty() && g_creatorSaveHistoryIndex > 0;
    const bool canForward = !g_creatorSaveHistory.empty() && g_creatorSaveHistoryIndex + 1 < g_creatorSaveHistory.size();
    if (!canBack) ImGui::BeginDisabled();
    if (ImGui::Button(Tr("<"), ImVec2(26.0f, 30.0f)) && canBack) {
        --g_creatorSaveHistoryIndex;
        g_creatorFolderBrowsePath = g_creatorSaveHistory[g_creatorSaveHistoryIndex];
    }
    if (!canBack) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canForward) ImGui::BeginDisabled();
    if (ImGui::Button(Tr(">"), ImVec2(26.0f, 30.0f)) && canForward) {
        ++g_creatorSaveHistoryIndex;
        g_creatorFolderBrowsePath = g_creatorSaveHistory[g_creatorSaveHistoryIndex];
    }
    if (!canForward) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(Tr("^"), ImVec2(26.0f, 30.0f))) {
        const fs::path parent = g_creatorFolderBrowsePath.parent_path();
        if (!parent.empty()) CreatorNavigateSaveFolder(parent);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    static char location[512] = {};
    const std::string locationText = CreatorRelativePath(g_creatorFolderBrowsePath);
    if (std::strcmp(location, locationText.c_str()) != 0) std::snprintf(location, sizeof(location), "%s", locationText.c_str());
    if (ImGui::InputText(Tr("##CreatorSaveLocation"), location, sizeof(location), ImGuiInputTextFlags_EnterReturnsTrue)) {
        fs::path target = fs::path(location);
        if (target.is_relative()) target = gameRootPath / target;
        CreatorNavigateSaveFolder(target);
    }
    ImGui::EndChild();

    ImGui::BeginChild(Tr("##CreatorSaveFileList"), ImVec2(0.0f, 0.0f), true);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    std::vector<fs::path> folders;
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(g_creatorFolderBrowsePath, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code typeEc;
        if (it->is_directory(typeEc) && !typeEc) {
            folders.push_back(it->path());
            continue;
        }
        if (it->is_regular_file(typeEc) && !typeEc && ToLower(it->path().extension().string()) == ".dds") files.push_back(it->path());
    }
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) { return ToLower(a.filename().string()) < ToLower(b.filename().string()); });
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return ToLower(a.filename().string()) < ToLower(b.filename().string()); });

    for (const fs::path& folder : folders) {
        const std::string label = std::string(Tr("FOLDER")) + "  " + folder.filename().string() + "##" + folder.generic_string();
        DrawBrowserSelectable(label.c_str(), false);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) CreatorNavigateSaveFolder(folder);
    }
    for (const fs::path& file : files) {
        ImGui::PushID(("SAVEFILE:" + file.generic_string()).c_str());
        const float xWidth = 20.0f;
        const float rowTextY = ImGui::GetCursorScreenPos().y;
        const float rowY = ImGui::GetCursorPosY();
        const float rowHeight = kBrowserRowHeight;
        const float available = ImGui::GetContentRegionAvail().x;
        if (DrawBrowserSelectable(file.filename().string().c_str(), false, ImGuiSelectableFlags_None,
                                  ImVec2(std::max(0.0f, available - xWidth), kBrowserRowHeight))) {
            std::snprintf(g_creatorSaveName, sizeof(g_creatorSaveName), "%s", file.filename().string().c_str());
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosY(rowY + (rowHeight - 20.0f) * 0.5f);
        if (DrawDeleteXButton("##DeleteSaveFile", rowTextY)) RequestCreatorDeleteTexture(file);
        ImGui::PopID();
    }
    if (folders.empty() && files.empty()) ImGui::TextDisabled(Tr("THIS FOLDER IS EMPTY"));
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::BeginChild(Tr("##CreatorSaveBottom"), ImVec2(0.0f, bottomHeight), false);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
    ImGui::TextDisabled(Tr("FILE NAME"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(Tr("##CreatorSaveName"), g_creatorSaveName, sizeof(g_creatorSaveName));
    if (savePressed) {
        std::string fileName = g_creatorSaveName;
        if (fileName.empty()) fileName = "new_texture.dds";
        fs::path output = g_creatorFolderBrowsePath / fileName;
        if (ToLower(output.extension().string()) != ".dds") output += ".dds";
        output = output.lexically_normal();
        if (!CreatorPathInsideGame(output)) {
            ImGui::OpenPopup(Tr("##CreatorSavePathError"));
        } else {
            std::error_code existsEc;
            const bool exists = fs::exists(output, existsEc);
            if (exists) {
                g_creatorPendingReplaceKind = g_creatorSaveKind;
                g_creatorPendingReplaceFormat = g_creatorSaveFormat;
                g_creatorPendingReplacePath = output;
                g_creatorOpenReplacePopup = true;
                ImGui::CloseCurrentPopup();
            } else if (SaveCreatorTextureNow(g_creatorSaveKind, output, g_creatorSaveFormat)) {
                ReleasePreviewForPath(output);
                const std::string savedName = output.filename().string();
                if (g_creatorSaveKind == CreatorSaveKind::Normal) {
                    std::snprintf(g_materialCreatorNormalName, sizeof(g_materialCreatorNormalName), "%s", savedName.c_str());
                    g_materialCreatorNormalFormat = g_creatorSaveFormat == 2 ? 1 : 0;
                } else if (g_creatorSaveKind == CreatorSaveKind::Gloss) {
                    std::snprintf(g_materialCreatorGlossName, sizeof(g_materialCreatorGlossName), "%s", savedName.c_str());
                    g_materialCreatorGlossFormat = g_creatorSaveFormat == 2 ? 1 : 0;
                } else {
                    std::snprintf(g_materialCreatorBumpName, sizeof(g_materialCreatorBumpName), "%s", savedName.c_str());
                    g_creatorBumpFormat = g_creatorSaveFormat == 2 ? 1 : 0;
                }
                InvalidateBrowserCaches();
                g_creatorSaveKind = CreatorSaveKind::None;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginPopupModal(Tr("##CreatorSavePathError"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(Tr("The selected folder must be inside the game root."));
        if (ImGui::Button(Tr("OK"), ImVec2(100.0f, 30.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::EndPopup();
}

void ResetCreatorNormalSettings() {
    g_materialCreatorNormalStrength = 2.0f;
    g_materialCreatorNormalSharpness = 0.0f;
    g_materialCreatorNormalHeightChannel = 0;
    g_materialCreatorNormalInvertHeight = false;
    g_materialCreatorNormalBlackPoint = 0.0f;
    g_materialCreatorNormalWhitePoint = 1.0f;
    g_materialCreatorNormalSmoothing = 0.0f;
    g_materialCreatorNormalFilter = 0;
    g_materialCreatorNormalTileEdges = false;
    g_materialCreatorFlipX = false;
    g_materialCreatorFlipY = true;
    g_materialCreatorFullZRange = false;
    g_materialCreatorNormalMipmaps = true;
    g_materialCreatorNormalFormat = 0;
    GenerateCreatorNormalPreview();
}

void ResetCreatorGlossSettings() {
    g_materialCreatorGlossSharpness = 0.0f;
    g_materialCreatorGlossContrast = 1.0f;
    g_materialCreatorGlossBrightness = 0.0f;
    g_materialCreatorGlossPower = 1.0f;
    g_materialCreatorGlossInvert = false;
    g_materialCreatorGlossLower = 0.0f;
    g_materialCreatorGlossUpper = 1.0f;
    g_materialCreatorGlossNormalize = true;
    g_materialCreatorGlossSource = 0;
    g_materialCreatorGlossSoftness = 0.0f;
    g_materialCreatorGlossMipmaps = true;
    g_materialCreatorGlossFormat = 0;
    GenerateCreatorGlossPreview();
}

void ResetCreatorBumpSettings() {
    g_creatorBumpHeightChannel = 1;
    g_creatorBumpSharpness = 0.0f;
    g_creatorBumpInvert = false;
    g_creatorBumpContrast = 1.0f;
    g_creatorBumpBrightness = 0.0f;
    g_creatorBumpNormalize = false;
    g_creatorBumpBlackPoint = 0.0f;
    g_creatorBumpWhitePoint = 1.0f;
    g_creatorBumpGamma = 1.0f;
    g_creatorBumpSmoothing = 0.0f;
    g_creatorBumpTileEdges = false;
    g_creatorBumpMipmaps = true;
    g_creatorBumpFormat = 0;
    GenerateCreatorBumpPreview();
}

void DrawMaterialCreator(int display_w, int display_h, EditorConfig& editorCfg) {
    (void)display_w;
    (void)display_h;
    (void)editorCfg;

    ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("TEXTURE GENERATOR"));
    ImGui::Spacing();

    static int creatorTab = 0;
    if (ImGui::BeginTabBar(Tr("##CreatorTabs"))) {
        if (ImGui::BeginTabItem(Tr("NORMAL MAP"))) {
            creatorTab = 0;
            ImGui::BeginChild(Tr("##NormalCreatorBody"), ImVec2(0.0f, -38.0f), false);
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const CreatorPanelsLayout layout = GetCreatorPanelsLayout();
            ImGui::BeginChild(Tr("##NormalPreviewPanel"), ImVec2(layout.previewWidth, layout.previewHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawCreatorImagePreview("NORMAL PREVIEW", g_creatorNormalPreview);
            ImGui::EndChild();
            if (!layout.stacked) ImGui::SameLine(0.0f, gap);
            ImGui::BeginChild(Tr("##NormalControls"), ImVec2(0.0f, 0.0f), true);
            if (CreatorGridBegin("##NormalSettingsGrid")) {
                const char* heightChannels[] = { "Luminance", "Red", "Green", "Blue", "Alpha" };
                const char* filters[] = { "Sobel", "Central difference", "Scharr" };
                const char* normalFormats[] = { "BC5", "BC7" };
                CreatorGridSection("HEIGHT INPUT");
                if (CreatorGridCombo("Height channel", &g_materialCreatorNormalHeightChannel, heightChannels, 5, "Select which source channel is interpreted as height. Luminance combines RGB.")) GenerateCreatorNormalPreview();
                if (CreatorGridCheckbox("Invert height", &g_materialCreatorNormalInvertHeight, "Swap raised and recessed areas before calculating normals.")) GenerateCreatorNormalPreview();
                if (CreatorGridSlider("Black point", &g_materialCreatorNormalBlackPoint, 0.0f, 1.0f, "Source values at or below this level become the minimum height.")) {
                    if (g_materialCreatorNormalBlackPoint > g_materialCreatorNormalWhitePoint) g_materialCreatorNormalWhitePoint = g_materialCreatorNormalBlackPoint;
                    GenerateCreatorNormalPreview();
                }
                if (CreatorGridSlider("White point", &g_materialCreatorNormalWhitePoint, 0.0f, 1.0f, "Source values at or above this level become the maximum height.")) {
                    if (g_materialCreatorNormalWhitePoint < g_materialCreatorNormalBlackPoint) g_materialCreatorNormalBlackPoint = g_materialCreatorNormalWhitePoint;
                    GenerateCreatorNormalPreview();
                }
                if (CreatorGridSlider("Smoothing", &g_materialCreatorNormalSmoothing, 0.0f, 1.0f, "Blend toward a Gaussian blur before calculating normals to reduce speckle.")) GenerateCreatorNormalPreview();
                if (CreatorGridSlider("Sharpening", &g_materialCreatorNormalSharpness, 0.0f, 4.0f, "Increase local source contrast before deriving height; this can amplify noise.")) GenerateCreatorNormalPreview();
                CreatorGridSection("NORMAL SETTINGS");
                if (CreatorGridSlider("Strength", &g_materialCreatorNormalStrength, 0.0f, 8.0f, "Controls how strongly height changes tilt the surface. Zero produces a flat normal map.")) GenerateCreatorNormalPreview();
                if (CreatorGridCombo("Gradient filter", &g_materialCreatorNormalFilter, filters, 3, "Choose how neighboring height samples are converted into X/Y surface slopes.")) GenerateCreatorNormalPreview();
                if (CreatorGridCheckbox("Wrap edges", &g_materialCreatorNormalTileEdges, "Wrap sampling across image borders to avoid seams on repeating textures.")) GenerateCreatorNormalPreview();
                if (CreatorGridCheckbox("Flip X", &g_materialCreatorFlipX, "Reverse the red-channel direction to match the model tangent-space convention.")) GenerateCreatorNormalPreview();
                if (CreatorGridCheckbox("Flip Y", &g_materialCreatorFlipY, "Reverse the green-channel direction; commonly needed for OpenGL/DirectX convention changes.")) GenerateCreatorNormalPreview();
                if (CreatorGridCheckbox("Full-range Z", &g_materialCreatorFullZRange, "Change blue-channel encoding; leave off for the usual remapped normal encoding.")) GenerateCreatorNormalPreview();
                CreatorGridSection("OUTPUT");
                if (CreatorGridCheckbox("Mipmaps", &g_materialCreatorNormalMipmaps, "Write smaller filtered levels for distant or minified surfaces.")) GenerateCreatorNormalPreview();
                if (CreatorGridCombo("DDS format", &g_materialCreatorNormalFormat, normalFormats, 2, "BC5 stores two tangent-space directions efficiently; BC7 also retains the blue channel.")) GenerateCreatorNormalPreview();
                CreatorGridEnd();
            }
            if (ImGui::Button(Tr("RESET SETTINGS"), ImVec2(-1.0f, 0.0f))) ResetCreatorNormalSettings();
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(Tr("GLOSS MAP"))) {
            creatorTab = 1;
            ImGui::BeginChild(Tr("##GlossCreatorBody"), ImVec2(0.0f, -38.0f), false);
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const CreatorPanelsLayout layout = GetCreatorPanelsLayout();
            ImGui::BeginChild(Tr("##GlossPreviewPanel"), ImVec2(layout.previewWidth, layout.previewHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawCreatorImagePreview("GLOSS PREVIEW", g_creatorGlossPreview);
            ImGui::EndChild();
            if (!layout.stacked) ImGui::SameLine(0.0f, gap);
            ImGui::BeginChild(Tr("##GlossControls"), ImVec2(0.0f, 0.0f), true);
            if (CreatorGridBegin("##GlossSettingsGrid")) {
                const char* glossSources[] = { "Luminance", "Red channel", "Green channel", "Blue channel", "Alpha channel" };
                const char* glossFormats[] = { "BC4", "BC7" };
                CreatorGridSection("SOURCE");
                if (CreatorGridCombo("Gloss source", &g_materialCreatorGlossSource, glossSources, 5, "Choose luminance or a source channel to use as the gloss mask.")) GenerateCreatorGlossPreview();
                if (CreatorGridSlider("Minimum level", &g_materialCreatorGlossLower, 0.0f, 1.0f, "Values below this become black when normalization is enabled.")) {
                    if (g_materialCreatorGlossLower > g_materialCreatorGlossUpper) g_materialCreatorGlossUpper = g_materialCreatorGlossLower;
                    GenerateCreatorGlossPreview();
                }
                if (CreatorGridSlider("Maximum level", &g_materialCreatorGlossUpper, 0.0f, 1.0f, "Values above this become white when normalization is enabled.")) {
                    if (g_materialCreatorGlossUpper < g_materialCreatorGlossLower) g_materialCreatorGlossLower = g_materialCreatorGlossUpper;
                    GenerateCreatorGlossPreview();
                }
                if (CreatorGridCheckbox("Normalize range", &g_materialCreatorGlossNormalize, "Stretch the selected minimum/maximum range across the full gloss range.")) GenerateCreatorGlossPreview();
                if (CreatorGridSlider("Softness", &g_materialCreatorGlossSoftness, 0.0f, 1.0f, "Blend the linear response toward smoothstep.")) GenerateCreatorGlossPreview();
                CreatorGridSection("ADJUSTMENTS");
                if (CreatorGridSlider("Contrast", &g_materialCreatorGlossContrast, 0.0f, 4.0f, "Expand or compress values around middle gray.")) GenerateCreatorGlossPreview();
                if (CreatorGridSlider("Brightness", &g_materialCreatorGlossBrightness, -1.0f, 1.0f, "Add or subtract a constant from every gloss value.")) GenerateCreatorGlossPreview();
                if (CreatorGridSlider("Gamma", &g_materialCreatorGlossPower, 0.05f, 8.0f, "Below 1 brightens midtones; above 1 darkens them.")) GenerateCreatorGlossPreview();
                if (CreatorGridSlider("Sharpening", &g_materialCreatorGlossSharpness, 0.0f, 4.0f, "Increase local source contrast before extracting gloss; this can amplify noise.")) GenerateCreatorGlossPreview();
                if (CreatorGridCheckbox("Invert mask", &g_materialCreatorGlossInvert, "Swap matte and glossy regions after extracting the selected source.")) GenerateCreatorGlossPreview();
                CreatorGridSection("OUTPUT");
                if (CreatorGridCheckbox("Mipmaps", &g_materialCreatorGlossMipmaps, "Write smaller filtered levels for distant or minified surfaces.")) GenerateCreatorGlossPreview();
                if (CreatorGridCombo("DDS format", &g_materialCreatorGlossFormat, glossFormats, 2, "BC4 is compact for a single-channel gloss mask; BC7 stores it in RGBA.")) GenerateCreatorGlossPreview();
                CreatorGridEnd();
            }
            if (ImGui::Button(Tr("RESET SETTINGS"), ImVec2(-1.0f, 0.0f))) ResetCreatorGlossSettings();
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(Tr("BUMP MAP"))) {
            creatorTab = 2;
            ImGui::BeginChild(Tr("##BumpCreatorBody"), ImVec2(0.0f, -38.0f), false);
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const CreatorPanelsLayout layout = GetCreatorPanelsLayout();
            ImGui::BeginChild(Tr("##BumpPreviewPanel"), ImVec2(layout.previewWidth, layout.previewHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            DrawCreatorImagePreview("BUMP PREVIEW", g_creatorBumpPreview);
            ImGui::EndChild();
            if (!layout.stacked) ImGui::SameLine(0.0f, gap);
            ImGui::BeginChild(Tr("##BumpControls"), ImVec2(0.0f, 0.0f), true);
            if (CreatorGridBegin("##BumpSettingsGrid")) {
                const char* heightChannels[] = { "Luminance", "Red (PrimeXT)", "Green", "Blue", "Alpha" };
                const char* bumpFormats[] = { "BC4", "BC7" };
                CreatorGridSection("HEIGHT INPUT");
                if (CreatorGridCombo("Height channel", &g_creatorBumpHeightChannel, heightChannels, 5, "PrimeXT reads _hmap from its red channel. Select another channel only if the source stores height elsewhere.")) GenerateCreatorBumpPreview();
                if (CreatorGridCheckbox("Invert height", &g_creatorBumpInvert, "Swap raised and recessed areas.")) GenerateCreatorBumpPreview();
                if (CreatorGridSlider("Black point", &g_creatorBumpBlackPoint, 0.0f, 1.0f, "Source values at or below this level become the minimum height.")) {
                    if (g_creatorBumpBlackPoint > g_creatorBumpWhitePoint) g_creatorBumpWhitePoint = g_creatorBumpBlackPoint;
                    GenerateCreatorBumpPreview();
                }
                if (CreatorGridSlider("White point", &g_creatorBumpWhitePoint, 0.0f, 1.0f, "Source values at or above this level become the maximum height.")) {
                    if (g_creatorBumpWhitePoint < g_creatorBumpBlackPoint) g_creatorBumpBlackPoint = g_creatorBumpWhitePoint;
                    GenerateCreatorBumpPreview();
                }
                if (CreatorGridSlider("Gamma", &g_creatorBumpGamma, 0.1f, 4.0f, "Adjust the distribution of height values after level remapping. 1.0 leaves it unchanged.")) GenerateCreatorBumpPreview();
                if (CreatorGridSlider("Smoothing", &g_creatorBumpSmoothing, 0.0f, 1.0f, "Blend toward a Gaussian blur to reduce small height variations.")) GenerateCreatorBumpPreview();
                if (CreatorGridSlider("Sharpening", &g_creatorBumpSharpness, 0.0f, 4.0f, "Increase local source contrast before extracting height; this can amplify noise.")) GenerateCreatorBumpPreview();
                CreatorGridSection("ADJUSTMENTS");
                if (CreatorGridSlider("Contrast", &g_creatorBumpContrast, 0.0f, 4.0f, "Expand or compress heights around middle gray.")) GenerateCreatorBumpPreview();
                if (CreatorGridSlider("Brightness", &g_creatorBumpBrightness, -1.0f, 1.0f, "Raise or lower the whole height range.")) GenerateCreatorBumpPreview();
                if (CreatorGridCheckbox("Normalize", &g_creatorBumpNormalize, "Stretch the remaining range to black and white. Off preserves authored PrimeXT _hmap values.")) GenerateCreatorBumpPreview();
                if (CreatorGridCheckbox("Wrap edges", &g_creatorBumpTileEdges, "Wrap the smoothing filter across image borders for repeating textures.")) GenerateCreatorBumpPreview();
                CreatorGridSection("OUTPUT");
                if (CreatorGridCheckbox("Mipmaps", &g_creatorBumpMipmaps, "Write smaller filtered levels for distant or minified surfaces.")) GenerateCreatorBumpPreview();
                if (CreatorGridCombo("DDS format", &g_creatorBumpFormat, bumpFormats, 2, "BC4 is compact for single-channel height; BC7 stores gray values in RGBA.")) GenerateCreatorBumpPreview();
                CreatorGridEnd();
            }
            if (ImGui::Button(Tr("RESET SETTINGS"), ImVec2(-1.0f, 0.0f))) ResetCreatorBumpSettings();
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (ImGui::Button(Tr("SAVE TEXTURE"), ImVec2(-1.0f, 34.0f))) {
        RequestCreatorSave(creatorTab == 0 ? CreatorSaveKind::Normal : (creatorTab == 1 ? CreatorSaveKind::Gloss : CreatorSaveKind::Bump));
    }

    DrawCreatorSavePopup();
    DrawCreatorReplacePopup();
    DrawCreatorDeletePopup();
}

void DrawEditorPanels(
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
    std::function<void(std::string&, int&)> refreshDataFunc
) {
    if (ImGui::BeginTabBar(Tr("EditorTabs"), ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem(Tr("Visual Materials (.mat)"))) {
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float panelHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y - 4.0f);
            const float colGap = ImGui::GetStyle().ItemSpacing.x;
            const float colWidth = std::max(180.0f, (availWidth - colGap * 2.0f) / 3.0f);

            ImGui::BeginChild(Tr("MatFilesChild"), ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("MAT FILE"));

            static char matFileFilter[128] = {};
            std::string matFilePreview = currentFileName;
            if (matFilePreview.empty()) matFilePreview = "SELECT .MAT FILE";
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo(Tr("##MatFileSelect"), matFilePreview.c_str())) {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(Tr("##MatFileFilter"), Tr("Search .mat files..."), matFileFilter, sizeof(matFileFilter));
                const std::string matFilter = ToLower(matFileFilter);
                bool hasFilteredFiles = false;
                for (const std::string& file : matFiles) {
                    if (!matFilter.empty() && ToLower(file).find(matFilter) == std::string::npos) continue;
                    hasFilteredFiles = true;
                    ImGui::PushID(file.c_str());
                    if (ImGui::Selectable(file.c_str(), currentFileName == file)) {
                        SelectMat(file, materials, currentMatIndex, currentFileName,
                                  editorCfg.autoAssignMaterialTextures);
                        editorCfg.lastMatFile = currentFileName;
                        matFileFilter[0] = '\0';
                    }
                    ImGui::PopID();
                }
                if (!hasFilteredFiles) ImGui::TextDisabled(Tr("NO .MAT FILES FOUND"));
                ImGui::EndCombo();
            }

            if (!matFiles.empty() && currentFileName != "None") {
                if (ImGui::Button(Tr("+ NEW MATERIAL"), ImVec2(-1.0f, 0.0f))) g_openNewMaterialPopup = true;
            }

            ImGui::Separator();

            if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("MATERIAL"));
                static char materialSearch[128] = {};
                std::string materialPreviewName = materials[currentMatIndex].name;
                const std::string previewExtension = ToLower(fs::path(materialPreviewName).extension().string());
                if (previewExtension == ".png" || previewExtension == ".tga" || previewExtension == ".dds") {
                    materialPreviewName = fs::path(materialPreviewName).stem().string();
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo(Tr("##MaterialSelect"), materialPreviewName.c_str())) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint(Tr("##MaterialSearch"), Tr("Search material..."), materialSearch, sizeof(materialSearch));
                    const std::string materialFilter = ToLower(materialSearch);
                    std::vector<size_t> materialOrder(materials.size());
                    std::iota(materialOrder.begin(), materialOrder.end(), 0);
                    std::sort(materialOrder.begin(), materialOrder.end(), [&](size_t a, size_t b) {
                        const std::string al = ToLower(materials[a].name);
                        const std::string bl = ToLower(materials[b].name);
                        if (al != bl) return al < bl;
                        return a < b;
                    });
                    bool hasFilteredMaterials = false;
                    for (size_t n : materialOrder) {
                        std::string materialLabel = materials[n].name;
                        const std::string ext = ToLower(fs::path(materialLabel).extension().string());
                        if (ext == ".png" || ext == ".tga" || ext == ".dds") materialLabel = fs::path(materialLabel).stem().string();
                        if (!materialFilter.empty() && ToLower(materialLabel).find(materialFilter) == std::string::npos) continue;
                        hasFilteredMaterials = true;
                        ImGui::PushID(static_cast<int>(n));
                        if (ImGui::Selectable(materialLabel.c_str(), currentMatIndex == static_cast<int>(n))) {
                            currentMatIndex = static_cast<int>(n);
                            materials[currentMatIndex].loadTextures();
                            materialSearch[0] = '\0';
                        }
                        ImGui::PopID();
                    }
                    if (!hasFilteredMaterials) ImGui::TextDisabled(Tr("NO MATERIALS FOUND"));
                    ImGui::EndCombo();
                }

                ImGui::TextDisabled(Tr("MATERIAL NAME"));
                static char materialNameBuffer[256] = {};
                static std::string materialNameBufferSource;
                if (materialNameBufferSource != materials[currentMatIndex].name) {
                    materialNameBufferSource = materials[currentMatIndex].name;
                    std::snprintf(materialNameBuffer, sizeof(materialNameBuffer), "%s", materials[currentMatIndex].name.c_str());
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText(Tr("##MaterialName"), materialNameBuffer, sizeof(materialNameBuffer))) {
                    std::string newName = materialNameBuffer;
                    if (!newName.empty() && newName != materials[currentMatIndex].name) {
                        materials[currentMatIndex].name = newName;
                        materialNameBufferSource = newName;
                        SaveMaterialsAndRefreshText(currentFileName, materials);
                    }
                }

                Material& mat = materials[currentMatIndex];
                bool textureFieldsChanged = false;
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                if (ImGui::BeginTable(Tr("##TextureFields"), 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableNextColumn();
                    textureFieldsChanged |= DrawResettableInput("diffuse", Tr("Diffuse"), mat.diffusePath, sizeof(mat.diffusePath), mat.diffuseVisible);
                    ImGui::TableNextColumn();
                    textureFieldsChanged |= DrawResettableInput("normal", Tr("Normal"), mat.normalPath, sizeof(mat.normalPath), mat.normalVisible);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    textureFieldsChanged |= DrawResettableInput("gloss", Tr("Gloss"), mat.glossPath, sizeof(mat.glossPath), mat.glossVisible);
                    ImGui::TableNextColumn();
                    textureFieldsChanged |= DrawResettableInput("luma", Tr("Luma"), mat.lumaPath, sizeof(mat.lumaPath), mat.lumaVisible);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    textureFieldsChanged |= DrawResettableInput("bump", Tr("Bump"), mat.bumpPath, sizeof(mat.bumpPath), mat.bumpVisible);
                    ImGui::TableNextColumn();
                    textureFieldsChanged |= DrawResettableInput("detail", Tr("Detail"), mat.detailPath, sizeof(mat.detailPath), mat.detailVisible);
                    ImGui::EndTable();
                }
                if (textureFieldsChanged) {
                    mat.syncParams();
                    mat.loadTextures();
                    SaveMaterialsAndRefreshText(currentFileName, materials);
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
            ImGui::BeginChild(Tr("MatParamsChild"), ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("Parameters & Model"));
            if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                Material& mat = materials[currentMatIndex];
                bool materialParamsChanged = false;
                if (ImGui::BeginTable("##MaterialParametersGrid", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableNextColumn();
                    materialParamsChanged |= ImGui::SliderFloat(Tr("Smoothness"), &mat.smoothness, 0.0f, 1.0f);
                    ImGui::TableNextColumn();
                    materialParamsChanged |= ImGui::SliderFloat(Tr("Reflect"), &mat.reflectScale, 0.0f, 1.0f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    materialParamsChanged |= ImGui::SliderFloat(Tr("Relief"), &mat.reliefScale, 0.0f, 1.0f);
                    ImGui::TableNextColumn();
                    materialParamsChanged |= ImGui::SliderFloat(Tr("Refract"), &mat.refractScale, 0.0f, 1.0f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    materialParamsChanged |= ImGui::SliderFloat(Tr("Abberation"), &mat.aberrationScale, 0.0f, 1.0f);
                    ImGui::EndTable();
                }
                ImGui::TextUnformatted(Tr("Texture Tiling"));
                static bool symmetricTiling = false;
                if (DrawCheckbox(Tr("Symmetric"), &symmetricTiling)) {
                    if (symmetricTiling) mat.textureScaleY = mat.textureScaleX;
                    materialParamsChanged = true;
                }
                if (symmetricTiling) {
                    materialParamsChanged |= ImGui::DragFloat(Tr("##TextureTilingSymmetric"), &mat.textureScaleX, 0.1f, 0.1f, 64.0f, "%.2f");
                    mat.textureScaleY = mat.textureScaleX;
                } else {
                    materialParamsChanged |= ImGui::DragFloat2(Tr("##TextureTiling"), &mat.textureScaleX, 0.1f, 0.1f, 64.0f, "%.2f");
                }
                mat.textureScaleX = std::clamp(mat.textureScaleX, 0.1f, 64.0f);
                mat.textureScaleY = std::clamp(mat.textureScaleY, 0.1f, 64.0f);
                std::vector<const char*> physMatPtrs;
                for (const auto& s : physicalMaterialTypes) physMatPtrs.push_back(s.c_str());
                if (!physMatPtrs.empty() && ImGui::Combo(Tr("Phys Material"), &mat.matTypeIndex, physMatPtrs.data(), static_cast<int>(physMatPtrs.size()))) {
                    if (mat.matTypeIndex >= 0 && static_cast<size_t>(mat.matTypeIndex) < physicalMaterialTypes.size()) {
                        for (auto& p : mat.params) if (p.first == "material") p.second = physicalMaterialTypes[mat.matTypeIndex];
                    }
                }
                if (ImGui::Button(Tr("Apply Changes"))) {
                    materialParamsChanged = true;
                    mat.syncParams();
                    mat.loadTextures();
                }
                ImGui::SameLine();
                if (materialParamsChanged) {
                    mat.smoothness = std::clamp(mat.smoothness, 0.0f, 1.0f);
                    mat.reflectScale = std::max(mat.reflectScale, 0.0f);
                    mat.reliefScale = std::max(mat.reliefScale, 0.0f);
                    mat.refractScale = std::max(mat.refractScale, 0.0f);
                    mat.aberrationScale = std::max(mat.aberrationScale, 0.0f);
                    mat.syncParams();
                    SaveMaterialsAndRefreshText(currentFileName, materials);
                }
                if (ImGui::Button(Tr("Save All"))) {
                    mat.syncParams();
                    SaveMaterialsAndRefreshText(currentFileName, materials);
                }
            }
            ImGui::Separator();
            const char* modelShapeItems[] = {
                Tr("Cube"), Tr("Sphere"), Tr("Plane"), Tr("Cylinder"),
                Tr("Cone"), Tr("Torus"), Tr("Newell Teapot")
            };
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo(Tr("Model Shape"), &shapeType, modelShapeItems, static_cast<int>(std::size(modelShapeItems)));
            ImGui::EndChild();
            ImGui::PopStyleVar(2);

            ImGui::SameLine();
            ImGui::BeginChild(Tr("MatLightChild"), ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("Lighting & Maps"));
            const bool hasSelectedMaterial = !materials.empty() && currentMatIndex >= 0 &&
                static_cast<std::size_t>(currentMatIndex) < materials.size();
            bool unavailableDetail = false;
            bool& detailEnabled = hasSelectedMaterial ? materials[currentMatIndex].detailVisible : unavailableDetail;
            if (ImGui::BeginTable("##MapIntensityGrid", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
                const auto mapRow = [](const char* mapLabel, bool* enabled, const char* intensityLabel, float* intensity) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    DrawCheckbox(Tr(mapLabel), enabled);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::SliderFloat(Tr(intensityLabel), intensity, 0.0f, 1.0f, "%.2f");
                };
                mapRow("Normal Map", &useNormal, "Normal Intensity", &normalIntensity);
                mapRow("Gloss Map", &useGloss, "Gloss Intensity", &glossIntensity);
                mapRow("Luma Map", &useLuma, "Luma Intensity", &lumaIntensity);
                mapRow("Use Bump", &useBump, "Bump Intensity", &bumpIntensity);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (hasSelectedMaterial) DrawCheckbox(Tr("Detail"), &detailEnabled);
                else ImGui::TextDisabled(Tr("Detail"));
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat(Tr("Detail Intensity"), &detailIntensity, 0.0f, 1.0f, "%.2f");
                ImGui::EndTable();
            }
            const char* lightModeItems[] = {Tr("Camera"), Tr("Fixed"), Tr("Dynamic")};
            ImGui::Combo(Tr("Light Mode"), &lightMode, lightModeItems, static_cast<int>(std::size(lightModeItems)));
            if (lightMode == 2) {
                ImGui::SliderFloat(Tr("Dynamic Speed"), &editorCfg.dynamicLightSpeed, 0.1f, 5.0f);
                ImGui::SliderFloat(Tr("Dynamic Radius"), &editorCfg.dynamicLightRadius, 1.0f, 6.0f);
            }
            const float intensityMax = editorCfg.allowHighLightIntensity ? 25.0f : 5.0f;
            ImGui::SliderFloat(Tr("Intensity"), &lightIntensity, 0.0f, intensityMax);
            lightIntensity = std::clamp(lightIntensity, 0.0f, intensityMax);
            ImGui::ColorEdit3(Tr("Color"), lightColor);
            glossIntensity = std::clamp(glossIntensity, 0.0f, 1.0f);
            normalIntensity = std::clamp(normalIntensity, 0.0f, 1.0f);
            lumaIntensity = std::clamp(lumaIntensity, 0.0f, 1.0f);
            bumpIntensity = std::clamp(bumpIntensity, 0.0f, 1.0f);
            detailIntensity = std::clamp(detailIntensity, 0.0f, 1.0f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(Tr("Physical Materials (.def)"))) {
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float panelHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y - 4.0f);
            const float colGap = ImGui::GetStyle().ItemSpacing.x;
            const float colWidth = std::max(220.0f, (availWidth - colGap) * 0.5f);

            ImGui::BeginChild(Tr("DefListChild"), ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("Physical Material Entries"));
            if (!physicalMaterials.empty() && currentPhysMatIndex >= 0 && static_cast<size_t>(currentPhysMatIndex) < physicalMaterials.size()) {
                if (ImGui::BeginCombo(Tr("Select Physical Material"), physicalMaterials[currentPhysMatIndex].name.c_str())) {
                    for (size_t n = 0; n < physicalMaterials.size(); ++n) {
                        if (ImGui::Selectable(physicalMaterials[n].name.c_str(), currentPhysMatIndex == static_cast<int>(n))) {
                            currentPhysMatIndex = static_cast<int>(n);
                            physicalMaterials[currentPhysMatIndex].updateBuffers();
                        }
                    }
                    ImGui::EndCombo();
                }
                static char defNameBuf[128] = {};
                static int lastDefIndex = -1;
                if (lastDefIndex != currentPhysMatIndex) {
                    std::snprintf(defNameBuf, sizeof(defNameBuf), "%s", physicalMaterials[currentPhysMatIndex].name.c_str());
                    defNameBuf[sizeof(defNameBuf) - 1] = '\0';
                    lastDefIndex = currentPhysMatIndex;
                }
                if (ImGui::InputText(Tr("Material Name"), defNameBuf, sizeof(defNameBuf))) physicalMaterials[currentPhysMatIndex].name = defNameBuf;
                if (ImGui::Button(Tr("Add New Def Entry"))) {
                    PhysicalMaterialEntry newEntry;
                    newEntry.name = "new_material_type";
                    newEntry.multiParams["impact_decal"] = {"shot"};
                    newEntry.updateBuffers();
                    physicalMaterials.push_back(newEntry);
                    currentPhysMatIndex = static_cast<int>(physicalMaterials.size()) - 1;
                }
            } else {
                ImGui::TextDisabled(Tr("No physical materials loaded"));
                if (ImGui::Button(Tr("Load / Create Default"))) {
                    fs::path defPath = gameRootPath / "scripts" / "materials.def";
                    if (!fs::exists(defPath)) defPath = gameRootPath / "materials.def";
                    LoadAllPhysicalMaterials(defPath.string(), physicalMaterials);
                    if (physicalMaterials.empty()) {
                        PhysicalMaterialEntry defMat;
                        defMat.name = "default";
                        defMat.multiParams["impact_decal"] = {"shot"};
                        defMat.updateBuffers();
                        physicalMaterials.push_back(defMat);
                    }
                    currentPhysMatIndex = 0;
                    physicalMaterials[currentPhysMatIndex].updateBuffers();
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild(Tr("DefParamsChild"), ImVec2(colWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive], "%s", Tr("Parameters Editor"));
            if (!physicalMaterials.empty() && currentPhysMatIndex >= 0 && static_cast<size_t>(currentPhysMatIndex) < physicalMaterials.size()) {
                PhysicalMaterialEntry& pMat = physicalMaterials[currentPhysMatIndex];
                ImGui::InputText(Tr("Impact Decal"), pMat.impactDecal, sizeof(pMat.impactDecal));
                ImGui::InputText(Tr("Impact Parts"), pMat.impactPartsBuf, sizeof(pMat.impactPartsBuf));
                ImGui::InputText(Tr("Impact Sound"), pMat.impactSoundBuf, sizeof(pMat.impactSoundBuf));
                ImGui::InputText(Tr("Step Sound"), pMat.stepSoundBuf, sizeof(pMat.stepSoundBuf));
                if (ImGui::Button(Tr("Apply Def Changes"))) pMat.syncParams();
                ImGui::SameLine();
                if (ImGui::Button(Tr("Save materials.def"))) {
                    pMat.syncParams();
                    fs::path defPath = gameRootPath / "scripts" / "materials.def";
                    if (!fs::exists(defPath.parent_path())) fs::create_directories(defPath.parent_path());
                    SaveAllPhysicalMaterials(defPath.string(), physicalMaterials);
                    physicalMaterialTypes = LoadPhysicalMaterialTypes();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    (void)display_w;
    (void)display_h;
    (void)currentDefFile;
    (void)refreshDataFunc;
}

}

bool IsMaterialCreatorOpen() {
    return g_materialCreator;
}

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
) {
    static std::string appliedTheme;
    if (appliedTheme != editorCfg.themeName) {
        ApplyTheme(editorCfg);
        appliedTheme = editorCfg.themeName;
    }
    static std::optional<fs::file_time_type> physicalDefsMtime;
    const fs::path physicalDefsPath = gameRootPath / "scripts" / "materials.def";
    std::error_code physicalDefsEc;
    const auto physicalDefsNow = fs::exists(physicalDefsPath, physicalDefsEc) ? fs::last_write_time(physicalDefsPath, physicalDefsEc) : fs::file_time_type{};
    if (!physicalDefsMtime || physicalDefsNow != *physicalDefsMtime) {
        physicalDefsMtime = physicalDefsNow;
        physicalMaterialTypes = LoadPhysicalMaterialTypes();
    }

    g_viewportHovered = false;
    g_viewportPos = ImVec2(0.0f, 0.0f);
    g_viewportSize = ImVec2(0.0f, 0.0f);
    g_viewportDrawList = nullptr;
    RebuildBrowser();

    const float width = static_cast<float>(std::max(1, display_w));
    const float height = static_cast<float>(std::max(1, display_h));
    const float gap = 6.0f;
    const bool leftVisible = g_materialCreator || g_showBrowser || g_showTexturePreview;
    const float leftWidth = leftVisible ? std::clamp(width * 0.26f, 300.0f, 390.0f) : 0.0f;
    const float bottomHeight = g_showMaterialEditor ? std::clamp(height * 0.31f, 255.0f, 315.0f) : 0.0f;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin(Tr("##MatEditLayout"), nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(Tr("Project"))) {
            if (ImGui::MenuItem(Tr("Load All WADs"))) {
                ScanAndLoadAllWads();
                SaveWads(editorCfg);
                ReleasePreview();
            }
            if (ImGui::MenuItem(Tr("Reload Saved WADs"))) {
                LoadWadArchives(editorCfg.loadedWads);
                ReleasePreview();
            }
            if (ImGui::MenuItem(Tr("Clear Loaded WADs"))) {
                ClearWadArchives();
                editorCfg.loadedWads.clear();
                SaveConfig(editorCfg);
                ReleasePreview();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(Tr("Auto-Gen"))) EnterMaterialCreator(materials, currentMatIndex, currentFileName);
            if (ImGui::MenuItem(Tr("Quick Create .MAT"))) g_showMatCreator = true;
            if (ImGui::MenuItem(Tr("Quick Create .DEF"))) g_showDefCreator = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Tr("Settings"))) {
            if (ImGui::MenuItem(Tr("Open Settings"))) g_showSettings = true;
            ImGui::Separator();
            if (ImGui::BeginMenu(Tr("Panels"))) {
                ImGui::MenuItem(Tr("File Browser"), nullptr, &g_showBrowser);
                ImGui::MenuItem(Tr("Texture Preview"), nullptr, &g_showTexturePreview);
                ImGui::MenuItem(Tr("Material Editor"), nullptr, &g_showMaterialEditor);
                ImGui::Separator();
                if (ImGui::MenuItem(Tr("Show All Panels"))) {
                    g_showBrowser = true;
                    g_showTexturePreview = true;
                    g_showMaterialEditor = true;
                }
                if (ImGui::MenuItem(Tr("Hide All Panels"))) {
                    g_showBrowser = false;
                    g_showTexturePreview = false;
                    g_showMaterialEditor = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Tr("Instruments"))) {
            if (ImGui::MenuItem(Tr("Reset Material View"))) {
                shapeType = 0;
                lightMode = 0;
                useNormal = true;
                useGloss = true;
                useLuma = true;
                useBump = true;
            }
            if (ImGui::MenuItem(Tr("Reset Lighting"))) {
                lightMode = 0;
                lightIntensity = 1.0f;
                lightColor[0] = 1.0f;
                lightColor[1] = 1.0f;
                lightColor[2] = 1.0f;
            }
            if (ImGui::MenuItem(Tr("Reload Current Material"))) {
                if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                    materials[currentMatIndex].loadTextures();
                    ReleasePreview();
                }
            }
            if (ImGui::MenuItem(Tr("Clear Texture Assignments"))) {
                if (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) {
                    Material& mat = materials[currentMatIndex];
                    mat.diffusePath[0] = '\0';
                    mat.normalPath[0] = '\0';
                    mat.glossPath[0] = '\0';
                    mat.lumaPath[0] = '\0';
                    mat.bumpPath[0] = '\0';
                    mat.detailPath[0] = '\0';
                    mat.syncParams();
                    mat.loadTextures();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(Tr("Help"))) {
            if (ImGui::MenuItem(Tr("About MatEdit"))) g_showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (leftVisible) {
        ImGui::BeginChild(Tr("##BrowserColumn"), ImVec2(leftWidth, 0.0f), false);

        if (g_materialCreator || g_showBrowser) {
            const float browserAvail = ImGui::GetContentRegionAvail().y;
            const float searchPadding = 2.0f;
            const float searchInputHeight = ImGui::GetFrameHeight();
            const float searchHeight = searchInputHeight + searchPadding * 2.0f;
            const float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
            const float desiredPreviewHeight = g_showTexturePreview ? std::clamp(browserAvail * 0.36f, 220.0f, 340.0f) : 0.0f;
            const float browserMinimumHeight = searchHeight + 24.0f + itemSpacingY;
            const float previewHeight = g_showTexturePreview
                ? std::min(desiredPreviewHeight, std::max(0.0f, browserAvail - browserMinimumHeight - itemSpacingY))
                : 0.0f;
            const float browserHeight = std::max(searchHeight + 1.0f,
                                                 browserAvail - previewHeight - (g_showTexturePreview ? itemSpacingY : 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 0.0f));
            ImGui::BeginChild(Tr("##FileTree"), ImVec2(0.0f, browserHeight), true);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            const float treeHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - searchHeight - 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 2.0f));
            ImGui::BeginChild(Tr("##FileTreeItems"), ImVec2(0.0f, treeHeight), false);
            const std::string creatorBrowserBefore = g_preview.reference;
            DrawDirectory(gameRootPath, std::string(g_searchBuffer), editorCfg, materials, physicalMaterials,
                          currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex);
            if (g_materialCreator && g_preview.reference != creatorBrowserBefore && !g_preview.reference.empty()) {
                SetCreatorDiffuse(g_preview.reference);
                GenerateCreatorNormalPreview();
                GenerateCreatorGlossPreview();
                GenerateCreatorBumpPreview();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::Dummy(ImVec2(0.0f, searchPadding));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(Tr("##FileSearch"), Tr("Search files / WAD textures..."), g_searchBuffer, sizeof(g_searchBuffer));
            ShowTooltip(Tr("Searches game files, material files, model textures and loaded WAD textures. Type part of a name or path."));
            ImGui::Dummy(ImVec2(0.0f, searchPadding));
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        if (g_showTexturePreview) {
            ImGui::BeginChild(Tr("##PreviewPanel"), ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            Material* activeMaterial = (!materials.empty() && currentMatIndex >= 0 && static_cast<size_t>(currentMatIndex) < materials.size()) ? &materials[currentMatIndex] : nullptr;
            DrawPreview(activeMaterial, leftWidth, currentFileName, materials);
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    if (leftVisible) ImGui::SameLine(0.0f, gap);

    const float rightWidth = std::max(100.0f, width - leftWidth - (leftVisible ? gap : 0.0f) - 10.0f);
    ImGui::BeginChild(Tr("##RightLayout"), ImVec2(rightWidth, 0.0f), true);

    static int topViewTab = 0;
    static std::vector<char> matTextBuffer;
    static std::vector<char> defTextBuffer;
    static std::string loadedMatTextPath;
    static std::string loadedDefTextPath;

    if (ImGui::Button(Tr("SCENE"), ImVec2(76.0f, 26.0f))) { topViewTab = 0; if (g_materialCreator) LeaveMaterialCreator(); }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button(Tr("MAT"), ImVec2(76.0f, 26.0f))) { topViewTab = 1; if (g_materialCreator) LeaveMaterialCreator(); }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button(Tr("DEF"), ImVec2(76.0f, 26.0f))) { topViewTab = 2; if (g_materialCreator) LeaveMaterialCreator(); }
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button(Tr("AUTO-GEN"), ImVec2(108.0f, 26.0f))) {
        EnterMaterialCreator(materials, currentMatIndex, currentFileName);
        topViewTab = 3;
    }

    if (g_materialCreator) {
        topViewTab = 3;
        DrawMaterialCreator(display_w, display_h, editorCfg);
    } else {

    const float rightAvail = ImGui::GetContentRegionAvail().y;
    const float centerHeight = std::max(80.0f, rightAvail - bottomHeight - (g_showMaterialEditor ? gap : 0.0f));

    if (topViewTab == 0) {
        ImGui::BeginChild(Tr("##ViewportSpacer"), ImVec2(0.0f, centerHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        g_viewportPos = ImGui::GetWindowPos();
        g_viewportSize = ImGui::GetWindowSize();
        g_viewportDrawList = ImGui::GetWindowDrawList();
        g_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImGui::EndChild();
    } else {
        ImGui::BeginChild(Tr("##TextDocument"), ImVec2(0.0f, centerHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (topViewTab == 1) {
            DrawTextDocumentEditor("##MatTextEditor", ".MAT", currentFileName, matTextBuffer, loadedMatTextPath, matFiles,
                                [&](const std::string& file) {
                                    SelectMat(file, materials, currentMatIndex, currentFileName,
                                              editorCfg.autoAssignMaterialTextures);
                                    editorCfg.lastMatFile = currentFileName;
                                },
                                [&]() {
                                    ReloadMaterialsPreserveSelection(currentFileName, materials, currentMatIndex,
                                                                     editorCfg.autoAssignMaterialTextures);
                                });
        } else {
            DrawTextDocumentEditor("##DefTextEditor", ".DEF", currentDefFile, defTextBuffer, loadedDefTextPath, defFiles,
                                [&](const std::string& file) {
                                    SelectDef(file, physicalMaterials, currentPhysMatIndex, currentDefFile);
                                    physicalMaterialTypes = LoadPhysicalMaterialTypes();
                                },
                                [&]() {
                                    ReloadPhysicalMaterialsPreserveSelection(currentDefFile, physicalMaterials, currentPhysMatIndex);
                                    physicalMaterialTypes = LoadPhysicalMaterialTypes();
                                });
        }
        ImGui::EndChild();
        g_viewportPos = ImVec2(0.0f, 0.0f);
        g_viewportSize = ImVec2(0.0f, 0.0f);
        g_viewportHovered = false;
        g_viewportDrawList = nullptr;
    }

    if (g_showMaterialEditor) {
        ImGui::BeginChild(Tr("##EditorPanels"), ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_None);
        DrawEditorPanels(display_w, display_h, editorCfg, materials, physicalMaterials, matFiles,
                            currentFileName, currentMatIndex, currentDefFile, currentPhysMatIndex,
                            shapeType, modelVisible, lightMode, useNormal, useGloss, useLuma, useBump,
                            lightIntensity, glossIntensity, normalIntensity, lumaIntensity, bumpIntensity, detailIntensity,
                            lightColor, refreshDataFunc);
        ImGui::EndChild();
    }
    }

    ImGui::EndChild();
    DrawTextureDeletePopup();
    DrawNewMaterialPopup(currentFileName, materials, currentMatIndex, editorCfg);

    static char settingsGamePath[1024] = {};
    static bool settingsInitialized = false;
    if (!settingsInitialized) {
        std::snprintf(settingsGamePath, sizeof(settingsGamePath), "%s", gameRootPath.string().c_str());
        settingsInitialized = true;
    }

    const ImVec2 mainCenter = ImGui::GetMainViewport()->GetCenter();

    if (g_showMatCreator) {
        ImGui::OpenPopup(Tr("Quick Create .MAT"));
        g_showMatCreator = false;
    }
    if (g_showDefCreator) {
        ImGui::OpenPopup(Tr("Quick Create .DEF"));
        g_showDefCreator = false;
    }

    ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(Tr("Quick Create .MAT"), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextDisabled(Tr("Creates a PrimeXT texture material in scripts/*.mat."));
        ImGui::InputTextWithHint(Tr("File"), Tr("materials.mat"), g_newMatFile, sizeof(g_newMatFile));
        ShowTooltip(Tr("MAT file name. The file is created inside scripts/. The .mat extension is added automatically if missing."));
        ImGui::InputTextWithHint(Tr("Texture / model texture"), Tr("wall1 or model/body"), g_newMatTexture, sizeof(g_newMatTexture));
        ShowTooltip(Tr("Base texture name or model texture reference used by the material. Keep it identical to the texture name in the WAD or model."));
        ImGui::TextDisabled(Tr("Tip: the texture name must match the WAD/model texture. Extra maps can use PrimeXT suffixes such as _norm and _gloss."));
        const char* phys = physicalMaterialTypes.empty() ? "default" : physicalMaterialTypes[0].c_str();
        static int createMatPhysIndex = 0;
        std::vector<const char*> physPtrs;
        for (const auto& name : physicalMaterialTypes) physPtrs.push_back(name.c_str());
        if (!physPtrs.empty()) {
            createMatPhysIndex = std::clamp(createMatPhysIndex, 0, static_cast<int>(physPtrs.size()) - 1);
            ImGui::Combo(Tr("Physical Material"), &createMatPhysIndex, physPtrs.data(), static_cast<int>(physPtrs.size()));
            ShowTooltip(Tr("Physical material type written to the MAT definition, such as concrete, wood or metal."));
            phys = physPtrs[createMatPhysIndex];
        }
        if (ImGui::Button(Tr("Create & Open"), ImVec2(130.0f, 0.0f))) {
            std::string fileName = g_newMatFile;
            if (fileName.empty()) fileName = "materials.mat";
            if (ToLower(fs::path(fileName).extension().string()) != ".mat") fileName += ".mat";
            fs::path outPath = gameRootPath / "scripts" / fileName;
            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);
            std::ofstream out(outPath);
            if (out.is_open()) {
                const std::string textureName = g_newMatTexture;
                out << "\"" << textureName << "\"\n{\n";
                if (phys && *phys) out << "\t\"material\"\t\"" << phys << "\"\n";
                out << "}\n";
                out.close();
                const std::string relative = fs::relative(outPath, gameRootPath).generic_string();
                std::vector<Material> createdMaterials;
                if (LoadAllMaterials(relative, createdMaterials, editorCfg.autoAssignMaterialTextures)) {
                    for (auto& material : materials) material.releaseTextures();
                    for (auto& material : createdMaterials) material.loadTextures();
                    materials = std::move(createdMaterials);
                    currentFileName = relative;
                    currentMatIndex = materials.empty() ? -1 : 0;
                    editorCfg.lastMatFile = relative;
                    SaveConfig(editorCfg);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(Tr("Quick Create .DEF"), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextDisabled(Tr("Creates a PrimeXT physical-material definition in scripts/*.def."));
        ImGui::InputTextWithHint(Tr("File"), Tr("materials.def"), g_newDefFile, sizeof(g_newDefFile));
        ImGui::InputTextWithHint(Tr("Material name"), Tr("concrete"), g_newDefName, sizeof(g_newDefName));
        ShowTooltip(Tr("Name of the physical material definition. This is the name used by the game when resolving the material type."));
        ImGui::InputTextWithHint(Tr("Impact decal"), Tr("shot"), g_newDefImpactDecal, sizeof(g_newDefImpactDecal));
        ShowTooltip(Tr("Decal name used when a bullet or impact hits this material."));
        ImGui::InputTextWithHint(Tr("Impact sounds"), Tr("materials/debris_concrete_01.wav materials/debris_concrete_02.wav"), g_newDefImpactSound, sizeof(g_newDefImpactSound));
        ShowTooltip(Tr("Space-separated impact sound paths. Up to 8 sounds are written to the DEF file."));
        ImGui::InputTextWithHint(Tr("Step sounds"), Tr("materials/walk_concrete_01.wav materials/walk_concrete_02.wav"), g_newDefStepSound, sizeof(g_newDefStepSound));
        ShowTooltip(Tr("Space-separated footstep sound paths. Up to 8 sounds are written to the DEF file."));
        ImGui::TextDisabled(Tr("Tip: sound paths are relative to sound/. Up to 8 impact/step sounds are supported."));
        if (ImGui::Button(Tr("Create & Open"), ImVec2(130.0f, 0.0f))) {
            std::string fileName = g_newDefFile;
            if (fileName.empty()) fileName = "materials.def";
            if (ToLower(fs::path(fileName).extension().string()) != ".def") fileName += ".def";
            fs::path outPath = gameRootPath / "scripts" / fileName;
            std::error_code ec;
            fs::create_directories(outPath.parent_path(), ec);
            std::ofstream out(outPath);
            if (out.is_open()) {
                out << "\"" << g_newDefName << "\"\n{\n";
                if (g_newDefImpactDecal[0]) out << "\t\"impact_decal\"\t\"" << g_newDefImpactDecal << "\"\n";
                auto writeList = [&](const char* key, const char* text) {
                    std::stringstream ss(text);
                    std::string item;
                    std::vector<std::string> values;
                    while (ss >> item && values.size() < 8) values.push_back(item);
                    if (!values.empty()) {
                        out << "\t\"" << key << "\"";
                        for (const auto& value : values) out << "\t\"" << value << "\"";
                        out << "\n";
                    }
                };
                writeList("impact_sound", g_newDefImpactSound);
                writeList("step_sound", g_newDefStepSound);
                out << "}\n";
                out.close();
                const std::string relative = fs::relative(outPath, gameRootPath).generic_string();
                currentDefFile = relative;
                LoadAllPhysicalMaterials(relative, physicalMaterials);
                currentPhysMatIndex = physicalMaterials.empty() ? -1 : 0;
                if (currentPhysMatIndex >= 0) physicalMaterials[currentPhysMatIndex].updateBuffers();
                physicalMaterialTypes = LoadPhysicalMaterialTypes();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static bool settingsWasOpen = false;
    static int settingsLastDisplayW = 0;
    static int settingsLastDisplayH = 0;
    if (!g_showSettings) settingsWasOpen = false;

    if (g_showSettings) {
        const fs::path skyboxEnvPath = ResolveSkyboxEnvPath(gameRootPath);
        const std::vector<std::string> skyboxes = FindSkyboxNames(skyboxEnvPath);
        if (skyboxes.empty() && !gameRootPath.empty()) {
            const fs::path envPath = gameRootPath / "gfx" / "env";
            std::error_code envError;
            if (!fs::is_directory(envPath, envError)) {
                ImGui::TextDisabled(Tr("SKYBOX DIRECTORY NOT FOUND: gfx/env"));
            }
        }

        if (editorCfg.skyboxName.empty() && !skyboxes.empty()) {
            const std::string defaultSkybox = skyboxes.front();
            const GLuint loaded = LoadSkyboxCubemap((skyboxEnvPath / defaultSkybox).string());
            if (loaded != 0) {
                skyboxTexture = loaded;
                editorCfg.skyboxName = defaultSkybox;
            }
        }

        const bool settingsViewportChanged = settingsLastDisplayW != display_w || settingsLastDisplayH != display_h;
        if (!settingsWasOpen || settingsViewportChanged) {
            ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        }
        ImGui::SetNextWindowSize(ImVec2(std::clamp(width * 0.62f, 620.0f, 820.0f), std::clamp(height * 0.84f, 560.0f, 780.0f)), ImGuiCond_Appearing);
        settingsLastDisplayW = display_w;
        settingsLastDisplayH = display_h;
        settingsWasOpen = true;
        ImVec4 settingsBg;
        ImVec4 settingsChildBg;
        if (editorCfg.themeName == "AMOLED") {
            settingsBg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            settingsChildBg = ImVec4(0.025f, 0.025f, 0.025f, 1.0f);
        } else if (editorCfg.themeName == "ImGui") {
            settingsBg = ImVec4(0.13f, 0.14f, 0.16f, 1.0f);
            settingsChildBg = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
        } else if (editorCfg.themeName == "Pastel Pink") {
            settingsBg = ImVec4(0.82f, 0.72f, 0.82f, 1.0f);
            settingsChildBg = ImVec4(0.90f, 0.80f, 0.88f, 1.0f);
        } else if (editorCfg.themeName == "Pastel Green") {
            settingsBg = ImVec4(0.12f, 0.16f, 0.14f, 1.0f);
            settingsChildBg = ImVec4(0.16f, 0.21f, 0.18f, 1.0f);
        } else {
            settingsBg = ImVec4(editorCfg.customThemeWindowBg[0], editorCfg.customThemeWindowBg[1], editorCfg.customThemeWindowBg[2], 1.0f);
            settingsChildBg = ImVec4(editorCfg.customThemeChildBg[0], editorCfg.customThemeChildBg[1], editorCfg.customThemeChildBg[2], 1.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_WindowBg, settingsBg);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, settingsChildBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        if (ImGui::Begin(Tr("Settings"), &g_showSettings, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const ImVec4 settingsFill = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            const ImVec2 settingsPos = ImGui::GetWindowPos();
            const ImVec2 settingsMin = ImGui::GetWindowContentRegionMin();
            const ImVec2 settingsMax = ImGui::GetWindowContentRegionMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(settingsPos.x + settingsMin.x, settingsPos.y + settingsMin.y),
                ImVec2(settingsPos.x + settingsMax.x, settingsPos.y + settingsMax.y),
                ImGui::ColorConvertFloat4ToU32(settingsFill));
            if (ImGui::BeginTabBar(Tr("SettingsTabs"))) {
                if (ImGui::BeginTabItem(Tr("General"))) {
                    ImGui::BeginTable(Tr("GeneralGrid"), 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_PadOuterX);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Tr("Language"));
                    const char* languageItems[] = {Tr("English"), Tr("Russian")};
                    int languageIndex = editorCfg.language == "ru" ? 1 : 0;
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo(Tr("##Language"), &languageIndex, languageItems, 2)) {
                        editorCfg.language = languageIndex == 1 ? "ru" : "en";
                        SetLanguage(editorCfg.language);
                        SaveConfig(editorCfg);
                    }
                    ImGui::Spacing();
                    ImGui::TextUnformatted(Tr("Game Root"));
                    const float applyWidth = 58.0f;
                    const float rootWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - applyWidth - 6.0f);
                    ImGui::SetNextItemWidth(rootWidth);
                    ImGui::InputText(Tr("##SettingsGameRoot"), settingsGamePath, sizeof(settingsGamePath));
                    ImGui::SameLine(0.0f, 6.0f);
                    if (ImGui::Button(Tr("Apply##GameRoot"), ImVec2(applyWidth, 0.0f))) {
                        fs::path newPath(settingsGamePath);
                        std::error_code ec;
                        newPath = fs::weakly_canonical(newPath, ec);
                        if (!ec && fs::is_directory(newPath)) {
                            gameRootPath = newPath;
                            editorCfg.gamePath = gameRootPath.string();
                            g_browserRoot.clear();
                            refreshDataFunc(currentFileName, currentMatIndex);
                            RebuildBrowser();
                            std::snprintf(settingsGamePath, sizeof(settingsGamePath), "%s", gameRootPath.string().c_str());
                        }
                    }

                    ImGui::Spacing();
                    ImGui::TextUnformatted(Tr("Anti-Aliasing"));
                    bool fxaaChanged = false;
                    bool taaChanged = false;
                    bool msaaChanged = false;
                    fxaaChanged = DrawCheckbox(Tr("FXAA"), &editorCfg.fxaaEnabled);
                    taaChanged = DrawCheckbox(Tr("TAA"), &editorCfg.taaEnabled);
                    msaaChanged = DrawCheckbox(Tr("MSAA"), &editorCfg.msaaEnabled);
                    if (fxaaChanged && editorCfg.fxaaEnabled) { editorCfg.taaEnabled = false; editorCfg.msaaEnabled = false; }
                    if (taaChanged && editorCfg.taaEnabled) { editorCfg.fxaaEnabled = false; editorCfg.msaaEnabled = false; }
                    if (msaaChanged && editorCfg.msaaEnabled) { editorCfg.fxaaEnabled = false; editorCfg.taaEnabled = false; }
                    ImGui::TextUnformatted(Tr("MSAA Samples"));
                    const char* sampleItems[] = { "2x", "4x", "8x" };
                    int sampleIndex = editorCfg.msaaSamples == 8 ? 2 : (editorCfg.msaaSamples == 2 ? 0 : 1);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::Combo(Tr("##MSAASamples"), &sampleIndex, sampleItems, 3)) {
                        editorCfg.msaaSamples = sampleIndex == 0 ? 2 : (sampleIndex == 1 ? 4 : 8);
                    }

                    ImGui::Spacing();
                    ImGui::TextUnformatted(Tr("Field of View"));
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputFloat(Tr("##FOV"), &editorCfg.fov, 1.0f, 5.0f, "%.0f deg")) editorCfg.fov = std::clamp(editorCfg.fov, 60.0f, 120.0f);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(Tr("Display"));
                    DrawCheckbox(Tr("Show FPS"), &editorCfg.showFps);
                    if (DrawCheckbox(Tr("VSync"), &editorCfg.vsyncEnabled)) {
                        glfwSwapInterval(editorCfg.vsyncEnabled ? 1 : 0);
                    }
                    if (DrawCheckbox(Tr("Texture Filtering"), &editorCfg.textureFilteringEnabled)) {
                        SetTextureFilteringEnabled(editorCfg.textureFilteringEnabled);
                    }
                    DrawCheckbox(Tr("Auto-assign material textures"), &editorCfg.autoAssignMaterialTextures);
                    DrawCheckbox(Tr("Allow Light Intensity > 5"), &editorCfg.allowHighLightIntensity);
                    if (!editorCfg.allowHighLightIntensity) lightIntensity = std::clamp(lightIntensity, 0.0f, 5.0f);

                    ImGui::Spacing();
                    ImGui::TextUnformatted(Tr("Viewport"));
                    DrawCheckbox(Tr("Unlimited Zoom In"), &editorCfg.unlimitedZoom);

                    ImGui::Spacing();
                    ImGui::TextUnformatted(Tr("WAD Loading"));
                    DrawCheckbox(Tr("Load all WAD files"), &editorCfg.autoLoadWads);
                    ImGui::EndTable();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(Tr("Keybinds"))) {
                    ImGui::TextUnformatted(Tr("Controls"));
                    ImGui::BeginTable(Tr("KeybindGrid"), 3, ImGuiTableFlags_SizingStretchSame);
                    const auto keyCell = [&](const char* label, int& key) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(label);
                        ImGui::TextUnformatted(Tr(label));
                        const bool waiting = g_rebindingKey == &key;
                        const std::string button = waiting ? Tr("PRESS A KEY...") : std::string(KeyName(key));
                        if (ImGui::Button(button.c_str(), ImVec2(-1.0f, 0.0f))) g_rebindingKey = &key;
                        if (waiting && g_editorInputWindow) {
                            for (int candidate = 32; candidate <= GLFW_KEY_LAST; ++candidate) {
                                if (glfwGetKey(g_editorInputWindow, candidate) == GLFW_PRESS && candidate != GLFW_KEY_UNKNOWN) {
                                    key = candidate;
                                    g_rebindingKey = nullptr;
                                    break;
                                }
                            }
                        }
                        ImGui::PopID();
                    };
                    keyCell("Toggle Panels", editorCfg.keyTogglePanels);
                    keyCell("Free Camera", editorCfg.keyFreeCam);
                    keyCell("Open Settings", editorCfg.keyOpenSettings);
                    keyCell("Move Forward", editorCfg.keyForward);
                    keyCell("Move Backward", editorCfg.keyBackward);
                    keyCell("Move Left", editorCfg.keyLeft);
                    keyCell("Move Right", editorCfg.keyRight);
                    keyCell("Move Up", editorCfg.keyUp);
                    keyCell("Move Down", editorCfg.keyDown);
                    keyCell("Fast Move", editorCfg.keySprint);
                    keyCell("Toggle Model", editorCfg.keyToggleModel);
                    ImGui::EndTable();
                    ImGui::Spacing();
                    ImGui::TextDisabled(Tr("Mouse orbit, pan and wheel zoom use the mouse and are not remappable."));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(Tr("Viewport"))) {
                    const std::string customDisplayName = editorCfg.customThemeName.empty() ? "My Theme" : editorCfg.customThemeName;
                    const bool customSelected = editorCfg.hasCustomTheme && editorCfg.themeName == customDisplayName;

                    ImGui::TextUnformatted(Tr("Themes"));
                    ImGui::BeginTable(Tr("ThemeTop"), 1, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV);
                    ImGui::TableNextColumn();
                    if (ImGui::BeginCombo(Tr("##Theme"), customSelected ? customDisplayName.c_str() : Tr(editorCfg.themeName.c_str()))) {
                        const char* builtins[] = {"ImGui", "Pastel Pink", "Pastel Green", "AMOLED"};
                        for (const char* theme : builtins) {
                            const bool selected = editorCfg.themeName == theme;
                            if (ImGui::Selectable(Tr(theme), selected)) {
                                editorCfg.themeName = theme;
                                ApplyTheme(editorCfg);
                            }
                        }
                        if (editorCfg.hasCustomTheme) {
                            const bool selected = customSelected;
                            if (ImGui::Selectable(customDisplayName.c_str(), selected)) {
                                editorCfg.themeName = customDisplayName;
                                ApplyTheme(editorCfg);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::EndTable();

                    static char newThemeName[128] = {};
                    static bool createThemeWindow = false;

                    ImGui::BeginTable(Tr("ThemeActions"), editorCfg.hasCustomTheme ? 3 : 2, ImGuiTableFlags_SizingStretchSame);
                    ImGui::TableNextColumn();
                    if (ImGui::Button(Tr("Create Theme"), ImVec2(-1.0f, 0.0f))) {
                        newThemeName[0] = '\0';
                        createThemeWindow = true;
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button(Tr("Apply"), ImVec2(-1.0f, 0.0f))) {
                        ApplyTheme(editorCfg);
                        appliedTheme = editorCfg.themeName;
                    }
                    if (editorCfg.hasCustomTheme) {
                        ImGui::TableNextColumn();
                        if (ImGui::Button(Tr("Delete Custom"), ImVec2(-1.0f, 0.0f))) {
                            editorCfg.hasCustomTheme = false;
                            editorCfg.themeName = "ImGui";
                            ApplyTheme(editorCfg);
                            SaveConfig(editorCfg);
                        }
                    }
                    ImGui::EndTable();

                    if (createThemeWindow) {
                        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Always);
                        if (ImGui::Begin(Tr("Create Theme"), &createThemeWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
                            ImGui::TextUnformatted(Tr("Theme Name"));
                            ImGui::SetNextItemWidth(-1.0f);
                            ImGui::InputText(Tr("##NewThemeName"), newThemeName, sizeof(newThemeName));
                            ImGui::Spacing();
                            if (ImGui::Button(Tr("Create"), ImVec2(150.0f, 0.0f))) {
                                std::string name = newThemeName[0] ? std::string(newThemeName) : "My Theme";
                                if (IsPresetTheme(name)) name += " Custom";
                                CaptureTheme(editorCfg);
                                editorCfg.customThemeName = name;
                                editorCfg.hasCustomTheme = true;
                                editorCfg.themeName = name;
                                ApplyTheme(editorCfg);
                                SaveConfig(editorCfg);
                                appliedTheme = editorCfg.themeName;
                                createThemeWindow = false;
                                newThemeName[0] = '\0';
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(Tr("Cancel"), ImVec2(150.0f, 0.0f))) {
                                createThemeWindow = false;
                                newThemeName[0] = '\0';
                            }
                        }
                        ImGui::End();
                    }

                    ImGui::Spacing();
                    if (customSelected) {
                        bool changedTheme = false;
                        const float cellWidth = (ImGui::GetContentRegionAvail().x - 5.0f * ImGui::GetStyle().ItemSpacing.x) / 6.0f;
                        ImGui::BeginTable(Tr("ThemeColorGrid"), 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH);
                        const auto drawColor = [&](const char* label, float* color, bool alpha = true) {
                            ImGui::TableNextColumn();
                            ImGui::PushID(label);
                            ImGui::TextUnformatted(Tr(label));
                            ImGui::SetNextItemWidth(std::max(40.0f, cellWidth));
                            changedTheme |= alpha
                                ? ImGui::ColorEdit4(Tr("##Color"), color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)
                                : ImGui::ColorEdit3(Tr("##Color"), color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            ImGui::PopID();
                        };
                        drawColor("Window", editorCfg.customThemeWindowBg);
                        drawColor("Panels", editorCfg.customThemeChildBg);
                        drawColor("Popup", editorCfg.customThemePopupBg);
                        drawColor("Fields", editorCfg.customThemeFrameBg);
                        drawColor("Fields Hover", editorCfg.customThemeFrameHovered);
                        drawColor("Fields Active", editorCfg.customThemeFrameActive);
                        drawColor("Buttons", editorCfg.customThemeButton);
                        drawColor("Button Hover", editorCfg.customThemeHeaderHovered);
                        drawColor("Button Active", editorCfg.customThemeHeaderActive);
                        drawColor("Text", editorCfg.customThemeText);
                        drawColor("Text Disabled", editorCfg.customThemeTextDisabled);
                        drawColor("Accent", editorCfg.customThemeAccent);
                        drawColor("Border", editorCfg.customThemeBorder);
                        drawColor("Title", editorCfg.customThemeTitleBg);
                        drawColor("Title Active", editorCfg.customThemeTitleBgActive);
                        drawColor("Menu Bar", editorCfg.customThemeMenuBarBg);
                        drawColor("Scroll Bar", editorCfg.customThemeScrollbarBg);
                        drawColor("Scroll Grab", editorCfg.customThemeScrollbarGrab);
                        drawColor("Header", editorCfg.customThemeHeader);
                        drawColor("Header Hover", editorCfg.customThemeHeaderHovered);
                        drawColor("Header Active", editorCfg.customThemeHeaderActive);
                        drawColor("Separator", editorCfg.customThemeSeparator);
                        drawColor("Resize Grip", editorCfg.customThemeResizeGrip);
                        drawColor("Tab", editorCfg.customThemeTab);
                        drawColor("Tab Hover", editorCfg.customThemeTabHovered);
                        drawColor("Tab Active", editorCfg.customThemeTabActive);
                        drawColor("Selected Text", editorCfg.customThemeTextSelected);
                        drawColor("Drag & Drop", editorCfg.customThemeDragDrop);
                        drawColor("Navigation", editorCfg.customThemeNav);
                        drawColor("Table Header", editorCfg.customThemeTableHeader);
                        drawColor("Table Border", editorCfg.customThemeTableBorder);
                        drawColor("Table Row", editorCfg.customThemeTableRow);
                        drawColor("Table Alt Row", editorCfg.customThemeTableRowAlt);
                        drawColor("Plot", editorCfg.customThemePlot);
                        drawColor("Plot Hover", editorCfg.customThemePlotHovered);
                        drawColor("Viewport BG", editorCfg.customThemeViewportBg, false);
                        ImGui::EndTable();
                        if (changedTheme) ApplyTheme(editorCfg);
                    } else {
                        ImGui::TextDisabled(Tr("Select a custom theme to edit colors."));
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted(Tr("Skybox"));
                    const char* currentSkybox = editorCfg.skyboxName.empty() ? "None" : editorCfg.skyboxName.c_str();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.58f);
                    if (ImGui::BeginCombo(Tr("##Skybox"), currentSkybox)) {
                        if (ImGui::Selectable(Tr("None"), editorCfg.skyboxName.empty())) {
                            editorCfg.skyboxName.clear();
                            if (skyboxTexture) { glDeleteTextures(1, &skyboxTexture); skyboxTexture = 0; }
                        }
                        for (const std::string& name : skyboxes) {
                            const bool selected = ToLower(editorCfg.skyboxName) == ToLower(name);
                            if (ImGui::Selectable(name.c_str(), selected)) {
                                const GLuint loaded = LoadSkyboxCubemap((skyboxEnvPath / name).string());
                                if (loaded != 0) {
                                    if (skyboxTexture) glDeleteTextures(1, &skyboxTexture);
                                    skyboxTexture = loaded;
                                    editorCfg.skyboxName = name;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled(skyboxes.empty()
                        ? Tr("No skyboxes found. Expected six files in gfx/env: namebk, namelf, namert, nameft, nameup, namedn.")
                        : "Expected six files in gfx/env: namebk, namelf, namert, nameft, nameup, namedn.");
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - 148.0f);
            if (ImGui::Button(Tr("Save Settings"), ImVec2(140.0f, 0.0f))) {
                editorCfg.gamePath = gameRootPath.string();
                editorCfg.lightMode = lightMode;
                SaveConfig(editorCfg);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    if (g_showAbout) ImGui::OpenPopup(Tr("About MatEdit"));
    const float aboutWidth = std::clamp(width * 0.62f, 560.0f, 760.0f);
    static bool aboutWasOpen = false;
    static int aboutLastDisplayW = 0;
    static int aboutLastDisplayH = 0;
    const bool aboutViewportChanged = aboutLastDisplayW != display_w || aboutLastDisplayH != display_h;
    if (g_showAbout && (!aboutWasOpen || aboutViewportChanged)) {
        ImGui::SetNextWindowPos(mainCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSize(ImVec2(aboutWidth, 0.0f), ImGuiCond_Appearing);
    aboutLastDisplayW = display_w;
    aboutLastDisplayH = display_h;
    aboutWasOpen = g_showAbout;
    if (ImGui::BeginPopupModal(Tr("About MatEdit"), &g_showAbout, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text(Tr("MatEdit"));
        ImGui::Separator();
        ImGui::TextWrapped(Tr("Material Editor for PrimeXT and similar projects running on Xash3D / Xash3D FWGS."));
        ImGui::TextWrapped(Tr("Created for editing game materials, textures and related material definitions used by these projects."));
        ImGui::Spacing();
        ImGui::TextWrapped(Tr("Author: hgruntt"));
        ImGui::TextWrapped(Tr("License: GPL-3.0"));
        ImGui::TextWrapped(Tr("MatEdit is absolutely free. If somebody charged you money for this program, you were scammed."));
        ImGui::Spacing();
        ImGui::TextWrapped(Tr("Third-party components include Dear ImGui, GLFW, GLM, GLI, GLAD and stb_image, each distributed under its respective license."));
        ImGui::Spacing();
        ImGui::TextDisabled(Tr("PROJECT CONTRIBUTORS"));
        const std::vector<std::string> contributors = GetContributors();
        ImGui::BeginChild(Tr("##ProjectContributors"), ImVec2(0.0f, 120.0f), true);
        if (contributors.empty()) {
            ImGui::TextDisabled(Tr("Contributor list is not available yet."));
        } else {
            for (const auto& contributor : contributors) ImGui::BulletText(Tr("%s"), contributor.c_str());
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (ImGui::Button(Tr("OK"), ImVec2(140.0f, 0.0f))) {
            g_showAbout = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    (void)defFiles;
    (void)ddsFiles;
}

void SetEditorInputWindow(GLFWwindow* window) { g_editorInputWindow = window; }

void OpenEditorSettings() { g_showSettings = true; }

void ToggleEditorPanels() {
    const bool show = !(g_showBrowser || g_showTexturePreview || g_showMaterialEditor);
    g_showBrowser = show;
    g_showTexturePreview = show;
    g_showMaterialEditor = show;
}

bool IsEditorViewportHovered() {
    return g_viewportHovered;
}

void GetEditorViewportRect(float& x, float& y, float& w, float& h) {
    x = g_viewportPos.x;
    y = g_viewportPos.y;
    w = g_viewportSize.x;
    h = g_viewportSize.y;
}

void DrawEditorViewportTexture(GLuint texture) {
    if (texture == 0 || g_viewportSize.x <= 1.0f || g_viewportSize.y <= 1.0f) return;
    ImDrawList* drawList = g_viewportDrawList ? g_viewportDrawList : ImGui::GetBackgroundDrawList();
    const float rounding = ImGui::GetStyle().ChildRounding;
    const ImVec2 maxPos(g_viewportPos.x + g_viewportSize.x, g_viewportPos.y + g_viewportSize.y);
    drawList->PushClipRect(g_viewportPos, maxPos, true);
    drawList->AddImageRounded(static_cast<ImTextureID>(texture), g_viewportPos, maxPos,
                              ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
                              IM_COL32_WHITE, rounding);
    drawList->AddRect(g_viewportPos, maxPos, ImGui::GetColorU32(ImGuiCol_Border), rounding);
    drawList->PopClipRect();
}

void DrawEditorViewportFPS(float fps) {
    if (!g_viewportDrawList || g_viewportSize.x <= 1.0f || g_viewportSize.y <= 1.0f) return;
    const ImVec2 maxPos(g_viewportPos.x + g_viewportSize.x, g_viewportPos.y + g_viewportSize.y);
    const char* textFormat = "FPS: %.1f";
    char text[32]{};
    std::snprintf(text, sizeof(text), textFormat, fps);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const float padX = 8.0f;
    const float padY = 5.0f;
    const ImVec2 textPos(maxPos.x - textSize.x - 14.0f, g_viewportPos.y + 10.0f);
    const ImVec2 bgMin(textPos.x - padX, textPos.y - padY);
    const ImVec2 bgMax(textPos.x + textSize.x + padX, textPos.y + textSize.y + padY);
    g_viewportDrawList->PushClipRect(g_viewportPos, maxPos, true);
    g_viewportDrawList->AddRectFilled(bgMin, bgMax, IM_COL32(0, 0, 0, 150), 4.0f);
    g_viewportDrawList->AddText(textPos, IM_COL32(255, 255, 255, 255), text);
    g_viewportDrawList->PopClipRect();
}


void ReleaseEditorUIPreview() {
    ReleaseTexturePreview(g_preview.info);
    g_preview.reference.clear();
    g_preview.kind.clear();
}

void InitUI() {
    SetupStyle();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.93f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
    c[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.16f, 0.19f, 0.96f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.32f, 0.38f, 0.55f);
    c[ImGuiCol_FrameBg] = ImVec4(0.21f, 0.23f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.31f, 0.39f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.38f, 0.48f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.65f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.72f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.24f, 0.36f, 0.54f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.46f, 0.69f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.55f, 0.83f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.24f, 0.36f, 0.54f, 0.70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.46f, 0.69f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.36f, 0.55f, 0.83f, 1.0f);
}
