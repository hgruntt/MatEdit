#include "EditorUI.h"
#include "imgui.h"
#include <iostream>
#include <fstream>
#define _CRT_SECURE_NO_WARNINGS

void SetupModernDarkStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Закругления
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    // Палитра цветов
    colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.93f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.16f, 0.19f, 0.94f);
    colors[ImGuiCol_Border]                 = ImVec4(0.30f, 0.32f, 0.38f, 0.50f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.21f, 0.23f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.28f, 0.31f, 0.39f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.34f, 0.38f, 0.48f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.17f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.22f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.13f, 0.14f, 0.16f, 0.75f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.18f, 0.19f, 0.23f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.16f, 0.17f, 0.20f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.35f, 0.38f, 0.46f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.45f, 0.49f, 0.58f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.55f, 0.59f, 0.69f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.24f, 0.36f, 0.54f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.46f, 0.69f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.36f, 0.55f, 0.83f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.24f, 0.36f, 0.54f, 0.70f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.46f, 0.69f, 0.80f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.36f, 0.55f, 0.83f, 1.00f);
    colors[ImGuiCol_Separator]              = colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.35f, 0.65f, 1.00f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.35f, 0.65f, 1.00f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.35f, 0.65f, 1.00f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.35f, 0.65f, 1.00f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.18f, 0.20f, 0.25f, 0.86f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.30f, 0.46f, 0.69f, 0.80f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.24f, 0.36f, 0.54f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.15f, 0.16f, 0.19f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.19f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.35f, 0.65f, 1.00f, 0.35f);
}

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
    float& lightIntensity,
    float* lightColor,
    GLuint& skyboxID,
    std::function<void(std::string&, int&)> refreshDataFunc
) {
    // Панель сверху (Root Path)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)display_w, 85));
    ImGui::Begin("Game Root Directory:", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    static char gamePathBuffer[512];
    strncpy(gamePathBuffer, gameRootPath.string().c_str(), sizeof(gamePathBuffer) - 1);
    ImGui::InputText("##GameDir", gamePathBuffer, sizeof(gamePathBuffer));
    if (ImGui::Button("Set & Refresh")) {
        if (fs::exists(gamePathBuffer) && fs::is_directory(gamePathBuffer)) {
            gameRootPath = gamePathBuffer;
            editorCfg.gamePath = gameRootPath.string();
            SaveConfig(editorCfg);
            refreshDataFunc(currentFileName, currentMatIndex);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create .mat")) ImGui::OpenPopup("New .mat");
    ImGui::SameLine();
    if (ImGui::Button("Create .def")) ImGui::OpenPopup("New .def");
        // Попап окно для создания нового .mat файла
        if (ImGui::BeginPopupModal("New .mat", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char newMatFileName[128] = "new_materials";
            ImGui::Text("Enter new .mat file name (inside scripts/ or root):");
            ImGui::InputText("##newMatFileName", newMatFileName, sizeof(newMatFileName));

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                std::string fileNameStr = std::string(newMatFileName);
                if (fileNameStr.find(".mat") == std::string::npos) {
                    fileNameStr += ".mat";
                }

                fs::path targetDir = gameRootPath / "scripts";
                if (!fs::exists(targetDir)) {
                    fs::create_directories(targetDir);
                }
                fs::path fullMatPath = targetDir / fileNameStr;

                if (!fs::exists(fullMatPath)) {
                    std::ofstream newFile(fullMatPath);
                    if (newFile.is_open()) {
                        newFile << "\"default_material\"\n{\n\t\"diffuseMap\"\t\"textures/default\"\n\t\"smoothness\"\t\"1.0\"\n}\n";
                        newFile.close();
                    }
                }

                refreshDataFunc(currentFileName, currentMatIndex);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
          // Попап для создания нового .def файла
        if (ImGui::BeginPopupModal("New .def", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char newDefFileName[128] = "materials.def";
            ImGui::Text("Enter new .def file name (inside scripts/ or root):");
            ImGui::InputText("##newDefFileName", newDefFileName, sizeof(newDefFileName));

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                std::string fileNameStr = std::string(newDefFileName);
                if (fileNameStr.find(".def") == std::string::npos) fileNameStr += ".def";

                fs::path targetDir = gameRootPath / "scripts";
                if (!fs::exists(targetDir)) fs::create_directories(targetDir);
                fs::path fullDefPath = targetDir / fileNameStr;

                if (!fs::exists(fullDefPath)) {
                    std::ofstream newFile(fullDefPath);
                    if (newFile.is_open()) {
                        newFile << "\"default\"\n{\n\t\"impact_decal\"\t\"shot\"\n\t\"impact_sound\"\t\"debris/concrete1.wav\"\n}\n";
                        newFile.close();
                    }
                }
                refreshDataFunc(currentFileName, currentMatIndex);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, display_h - 220));
    ImGui::SetNextWindowSize(ImVec2((float)display_w, 220));
    ImGui::Begin("Editor Panels", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

         if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_None)) {
            
            // Вкладка 1: Визуальные материалы (.mat)
            if (ImGui::BeginTabItem("Visual Materials (.mat)")) {
                float colWidth = (float)display_w / 3.0f;
                
                // Левая колончатость внутри вкладки
                ImGui::BeginChild("MatFilesChild", ImVec2(colWidth - 10, 170), true);
                ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Material Files");
                if (!matFiles.empty()) {
                    if (ImGui::BeginCombo("Select .mat File", currentFileName.c_str())) {
                        for (int n = 0; n < matFiles.size(); n++) {
                            if (ImGui::Selectable(matFiles[n].c_str(), currentFileName == matFiles[n])) {
                                currentFileName = matFiles[n];
                                editorCfg.lastMatFile = currentFileName;
                                materials.clear();
                                LoadAllMaterials(currentFileName, materials);
                                currentMatIndex = 0;
                                if(!materials.empty()) materials[currentMatIndex].loadTextures();
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1,0,0,1), "No .mat files found!");
                }

                if (!materials.empty() && currentMatIndex < materials.size()) {
                    static char nameBuffer[128];
                    static int lastMatIndex = -1;
                    if (lastMatIndex != currentMatIndex) {
                        strncpy(nameBuffer, materials[currentMatIndex].name.c_str(), 127);
                        lastMatIndex = currentMatIndex;
                    }
                    if (ImGui::BeginCombo("Select Material", materials[currentMatIndex].name.c_str())) {
                        for (int n = 0; n < materials.size(); n++) {
                            if (ImGui::Selectable(materials[n].name.c_str(), currentMatIndex == n)) {
                                currentMatIndex = n;
                                materials[currentMatIndex].loadTextures();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::InputText("Material Name", nameBuffer, 128)) {
                        materials[currentMatIndex].name = nameBuffer;
                    }
                    Material& mat = materials[currentMatIndex];
                    ImGui::InputText("Diffuse", mat.diffusePath, 256);
                    ImGui::InputText("Normal", mat.normalPath, 256);
                    ImGui::InputText("Gloss", mat.glossPath, 256);
                    ImGui::InputText("Luma", mat.lumaPath, 256);
                    ImGui::InputText("Detail", mat.detailPath, 256);
               }
                ImGui::EndChild();

                ImGui::SameLine();
                // Центральная колонка: параметры материала + модель
                ImGui::BeginChild("MatParamsChild", ImVec2(colWidth - 10, 170), true);
                ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Parameters & Model");
                if (!materials.empty() && currentMatIndex < materials.size()) {
                    Material& mat = materials[currentMatIndex];
                    ImGui::SliderFloat("Smoothness", &mat.smoothness, 0.0f, 1.0f);
                    ImGui::SliderFloat("Reflect", &mat.reflectScale, 0.0f, 1.0f);
                    ImGui::SliderFloat("Relief", &mat.reliefScale, 0.0f, 1.0f);
                    ImGui::SliderFloat("Refract", &mat.refractScale, 0.0f, 1.0f);
                    ImGui::SliderFloat("Abberation", &mat.aberrationScale, 0.0f, 1.0f);

                    std::vector<const char*> physMatPtrs;
                    for (const auto& s : physicalMaterialTypes) physMatPtrs.push_back(s.c_str());

                    if (ImGui::Combo("Phys Material", &mat.matTypeIndex, physMatPtrs.data(), static_cast<int>(physMatPtrs.size()))) {
                        if (mat.matTypeIndex >= 0 && mat.matTypeIndex < physicalMaterialTypes.size()) {
                            for(auto& p : mat.params) {
                                if(p.first == "material") p.second = physicalMaterialTypes[mat.matTypeIndex];
                            }
                        }
                    }
                    if (ImGui::Button("Apply Changes")) {
                        mat.syncParams();
                        mat.loadTextures();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save All")) {
                        materials[currentMatIndex].syncParams();
                        SaveAllMaterials(currentFileName, materials);
                    }
                }
                ImGui::Separator();
                ImGui::Combo("Model Shape", &shapeType, "Cube\0Sphere\0");
                ImGui::EndChild();

                ImGui::SameLine();
                // Правая колонка: Освещение
                ImGui::BeginChild("MatLightChild", ImVec2(colWidth - 10, 170), true);
                ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Lighting & Maps");
                ImGui::Checkbox("Normal Map", &useNormal);
                ImGui::Checkbox("Gloss Map", &useGloss);
                ImGui::Checkbox("Luma Map", &useLuma);

                ImGui::Combo("Light Mode", &lightMode, "Camera\0Fixed\0");
                ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, 5.0f);
                ImGui::ColorEdit3("Color", lightColor);
                ImGui::EndChild();

                ImGui::EndTabItem();
            }
       
            // Вкладка 2: Физические материалы (.def)
            if (ImGui::BeginTabItem("Physical Materials (.def)")) {
                float colWidth = (float)display_w / 2.0f - 15.0f;

                ImGui::BeginChild("DefListChild", ImVec2(colWidth, 170), true);
                ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Physical Material Entries (scripts/materials.def)");
                
                if (!physicalMaterials.empty()) {
                    if (ImGui::BeginCombo("Select Physical Material", physicalMaterials[currentPhysMatIndex].name.c_str())) {
                        for (int n = 0; n < physicalMaterials.size(); n++) {
                            if (ImGui::Selectable(physicalMaterials[n].name.c_str(), currentPhysMatIndex == n)) {
                                currentPhysMatIndex = n;
                                physicalMaterials[currentPhysMatIndex].updateBuffers();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    static char defNameBuf[128];
                    static int lastDefIndex = -1;
                    if (lastDefIndex != currentPhysMatIndex) {
                        strncpy(defNameBuf, physicalMaterials[currentPhysMatIndex].name.c_str(), sizeof(defNameBuf) - 1);
                        lastDefIndex = currentPhysMatIndex;
                    }
                    if (ImGui::InputText("Material Name", defNameBuf, sizeof(defNameBuf))) {
                        physicalMaterials[currentPhysMatIndex].name = defNameBuf;
                    }

                    if (ImGui::Button("Add New Def Entry")) {
                        PhysicalMaterialEntry newEntry;
                        newEntry.name = "new_material_type";
                        newEntry.multiParams["impact_decal"] = {"shot"};
                        newEntry.updateBuffers();
                        physicalMaterials.push_back(newEntry);
                        currentPhysMatIndex = static_cast<int>(physicalMaterials.size()) - 1;
                    }
                } else {
                    ImGui::TextColored(ImVec4(1,0,0,1), "No physical materials loaded from materials.def!");
                    if (ImGui::Button("Load / Create Default")) {
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

                ImGui::BeginChild("DefParamsChild", ImVec2(colWidth, 170), true);
                ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "Parameters Editor");
                if (!physicalMaterials.empty() && currentPhysMatIndex < physicalMaterials.size()) {
                    PhysicalMaterialEntry& pMat = physicalMaterials[currentPhysMatIndex];
                    
                    ImGui::InputText("Impact Decal", pMat.impactDecal, sizeof(pMat.impactDecal));
                    ImGui::InputText("Impact Parts", pMat.impactPartsBuf, sizeof(pMat.impactPartsBuf));
                    ImGui::InputText("Impact Sound", pMat.impactSoundBuf, sizeof(pMat.impactSoundBuf));
                    ImGui::InputText("Step Sound", pMat.stepSoundBuf, sizeof(pMat.stepSoundBuf));

                    if (ImGui::Button("Apply Def Changes")) {
                        pMat.syncParams();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save materials.def")) {
                        pMat.syncParams();
                        fs::path defPath = gameRootPath / "scripts" / "materials.def";
                        if (!fs::exists(defPath.parent_path())) fs::create_directories(defPath.parent_path());
                        SaveAllPhysicalMaterials(defPath.string(), physicalMaterials);
                        // Обновляем типы для .mat материалов
                        physicalMaterialTypes = LoadPhysicalMaterialTypes();
                    }
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    ImGui::End();
}

