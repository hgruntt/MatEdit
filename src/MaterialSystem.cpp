#include "MaterialSystem.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <gli/gli.hpp>
#define _CRT_SECURE_NO_WARNINGS

fs::path gameRootPath = "";
std::vector<std::string> physicalMaterialTypes = {"default"};

std::vector<std::string> LoadPhysicalMaterialTypes() {
    std::vector<std::string> types;
    fs::path defPath = gameRootPath / "scripts" / "materials.def";
    if (!fs::exists(defPath)) {
        defPath = gameRootPath / "materials.def";
    }
    
    std::ifstream file(defPath);
    if (!file.is_open()) {
        return {"default"};
    }

    std::string line;
    std::string lastLine;
    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                types.push_back(lastLine.substr(n1 + 1, n2 - n1 - 1));
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }

    if (types.empty()) types.push_back("default");
    return types;
}

GLuint LoadDDSTexture(const std::string& path) {
    fs::path fullPath;
    if (fs::path(path).is_absolute()) {
        fullPath = fs::path(path);
    } else {
        fullPath = gameRootPath / path;
    }

    std::string ddsFilePath = fullPath.string() + ".dds";
    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load DDS: " << ddsFilePath << std::endl;
        return 0;
    }
    gli::gl GL(gli::gl::PROFILE_GL33);

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(texture.levels() - 1));

    for (std::size_t level = 0; level < texture.levels(); ++level) {
        glCompressedTexImage2D(GL_TEXTURE_2D,
                               static_cast<GLint>(level),
                               GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
                               static_cast<GLsizei>(texture.extent(level).x),
                               static_cast<GLsizei>(texture.extent(level).y),
                               0,
                               static_cast<GLsizei>(texture.size(level)),
                               texture.data(0, 0, level));
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return textureID;
}

GLuint LoadDDS_Cubemap(const std::string& path) {
    gli::texture texture = gli::load(path + ".dds");
    if (texture.empty()) return 0;

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    gli::gl GL(gli::gl::PROFILE_GL33);

    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t level = 0; level < texture.levels(); ++level) {
            glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                   static_cast<GLint>(level),
                                   GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
                                   static_cast<GLsizei>(texture.extent(level).x),
                                   static_cast<GLsizei>(texture.extent(level).y),
                                   0,
                                   static_cast<GLsizei>(texture.size(level)),
                                   texture.data(face, 0, level));
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    
    return textureID;
}

GLuint LoadSkyboxAs2D(const std::string& path) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::string ddsFilePath = fullPath.string() + ".dds";

    gli::texture texture = gli::load(ddsFilePath);
    if (texture.empty()) {
        std::cerr << "Failed to load Skybox DDS: " << ddsFilePath << std::endl;
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    for (std::size_t level = 0; level < texture.levels(); ++level) {
        glCompressedTexImage2D(GL_TEXTURE_2D,
                               static_cast<GLint>(level),
                               GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
                               static_cast<GLsizei>(texture.extent(level).x),
                               static_cast<GLsizei>(texture.extent(level).y),
                               0,
                               static_cast<GLsizei>(texture.size(level)),
                               texture.data(0, 0, level));
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return textureID;
}

void Material::updateBuffers() {
    for(auto& p : params) {
        if(p.first == "diffuseMap") strncpy(diffusePath, p.second.c_str(), 255);
        if(p.first == "normalMap") strncpy(normalPath, p.second.c_str(), 255);
        if(p.first == "glossMap") strncpy(glossPath, p.second.c_str(), 255);
        if(p.first == "LumaMap") strncpy(lumaPath, p.second.c_str(), 255);
        if(p.first == "detailmap") strncpy(detailPath, p.second.c_str(), 255);
        if(p.first == "detailScale") strncpy(detailScale, p.second.c_str(), 63);
        if(p.first == "smoothness") try { smoothness = std::stof(p.second); } catch(...) {}
        if(p.first == "reflectScale") try { reflectScale = std::stof(p.second); } catch(...) {}
        if(p.first == "refractScale") try { refractScale = std::stof(p.second); } catch(...) {}
        if(p.first == "aberrationScale") try { aberrationScale = std::stof(p.second); } catch(...) {}
        if(p.first == "reliefScale") try { reliefScale = std::stof(p.second); } catch(...) {}
        if(p.first == "swayHeight") try { swayHeight = std::stoi(p.second); } catch(...) {}
        if(p.first == "material") {
            matTypeIndex = 0;
            for(size_t i = 0; i < physicalMaterialTypes.size(); i++) {
                if(p.second == physicalMaterialTypes[i]) {
                    matTypeIndex = static_cast<int>(i);
                    break;
                }
            }
        }   
    }
}

void Material::syncParams() {
    auto setParam = [&](const std::string& key, const std::string& val) {
        bool found = false;
        for(auto& p : params) {
            if(p.first == key) {
                p.second = val;
                found = true;
                break;
            }
        }
        if(!found) {
            params.push_back({key, val});
        }
    };

    setParam("diffuseMap", diffusePath);
    if(strlen(normalPath) > 0) setParam("normalMap", normalPath);
    if(strlen(glossPath) > 0) setParam("glossMap", glossPath);
    if(strlen(lumaPath) > 0) setParam("LumaMap", lumaPath);
    if(strlen(detailPath) > 0) setParam("detailmap", detailPath);
    setParam("detailScale", detailScale);
    setParam("smoothness", std::to_string(smoothness));
    setParam("reflectScale", std::to_string(reflectScale));
    setParam("refractScale", std::to_string(refractScale));
    setParam("aberrationScale", std::to_string(aberrationScale));
    setParam("reliefScale", std::to_string(reliefScale));
    setParam("swayHeight", std::to_string(swayHeight));
    
    if (matTypeIndex >= 0 && matTypeIndex < (int)physicalMaterialTypes.size()) {
        setParam("material", physicalMaterialTypes[matTypeIndex]);
    }
}

void Material::loadTextures() {
    for(auto const& [key, id] : textures) glDeleteTextures(1, &id);
    textures.clear();
    for(auto& p : params) {
        if(p.first == "diffuseMap") textures["diffuse"] = LoadDDSTexture(p.second);
        if(p.first == "normalMap") textures["normal"] = LoadDDSTexture(p.second);
        if(p.first == "glossMap") textures["gloss"] = LoadDDSTexture(p.second);
        if(p.first == "LumaMap") textures["luma"] = LoadDDSTexture(p.second);
    }
}

void PhysicalMaterialEntry::updateBuffers() {
    impactDecal[0] = '\0';
    impactPartsBuf[0] = '\0';
    impactSoundBuf[0] = '\0';
    stepSoundBuf[0] = '\0';

    auto joinVec = [](const std::vector<std::string>& vec) {
        std::string res;
        for (size_t i = 0; i < vec.size(); ++i) {
            res += vec[i];
            if (i + 1 < vec.size()) res += " ";
        }
        return res;
    };

    for (auto& [key, vals] : multiParams) {
        std::string joined = joinVec(vals);
        if (key == "impact_decal") strncpy(impactDecal, vals.empty() ? "" : vals[0].c_str(), sizeof(impactDecal) - 1);
        if (key == "impact_parts") strncpy(impactPartsBuf, joined.c_str(), sizeof(impactPartsBuf) - 1);
        if (key == "impact_sound") strncpy(impactSoundBuf, joined.c_str(), sizeof(impactSoundBuf) - 1);
        if (key == "step_sound") strncpy(stepSoundBuf, joined.c_str(), sizeof(stepSoundBuf) - 1);
    }
}

void PhysicalMaterialEntry::syncParams() {
    auto splitToVec = [](const std::string& str) {
        std::vector<std::string> res;
        std::stringstream ss(str);
        std::string item;
        while (ss >> item) {
            res.push_back(item);
        }
        return res;
    };

    if (strlen(impactDecal) > 0) multiParams["impact_decal"] = {impactDecal};
    else multiParams.erase("impact_decal");

    std::vector<std::string> parts = splitToVec(impactPartsBuf);
    if (!parts.empty()) multiParams["impact_parts"] = parts;
    else multiParams.erase("impact_parts");

    std::vector<std::string> impSounds = splitToVec(impactSoundBuf);
    if (!impSounds.empty()) multiParams["impact_sound"] = impSounds;
    else multiParams.erase("impact_sound");

    std::vector<std::string> stepSounds = splitToVec(stepSoundBuf);
    if (!stepSounds.empty()) multiParams["step_sound"] = stepSounds;
    else multiParams.erase("step_sound");
}

void LoadAllPhysicalMaterials(const std::string& path, std::vector<PhysicalMaterialEntry>& physMats) {
    physMats.clear();
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return;

    std::string line, lastLine;
    PhysicalMaterialEntry* currentMat = nullptr;

    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();

        if (line.find('{') != std::string::npos) {
            physMats.emplace_back();
            currentMat = &physMats.back();
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                currentMat->name = lastLine.substr(n1 + 1, n2 - n1 - 1);
            } else {
                currentMat->name = "Unnamed";
            }
        } else if (line.find('}') != std::string::npos) {
            if (currentMat) currentMat->updateBuffers();
            currentMat = nullptr;
        } else if (currentMat) {
            size_t q1 = line.find('\"');
            if (q1 == std::string::npos) continue;
            size_t q2 = line.find('\"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string key = line.substr(q1 + 1, q2 - q1 - 1);

            std::vector<std::string> values;
            size_t searchPos = q2 + 1;
            while (true) {
                size_t v1 = line.find('\"', searchPos);
                if (v1 == std::string::npos) break;
                size_t v2 = line.find('\"', v1 + 1);
                if (v2 == std::string::npos) break;
                values.push_back(line.substr(v1 + 1, v2 - v1 - 1));
                searchPos = v2 + 1;
            }
            if (!values.empty()) {
                currentMat->multiParams[key] = values;
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }
}

void SaveAllPhysicalMaterials(const std::string& path, const std::vector<PhysicalMaterialEntry>& physMats) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    if (!file.is_open()) return;

    for (const auto& mat : physMats) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& [key, vals] : mat.multiParams) {
            file << "\t\"" << key << "\"";
            for (const auto& v : vals) {
                file << "\t\"" << v << "\"";
            }
            file << "\n";
        }
        file << "}\n";
    }
}

void LoadAllMaterials(const std::string& path, std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ifstream file(fullPath);
    if (!file.is_open()) return;

    std::string line, lastLine;
    Material* currentMat = nullptr;

    while (std::getline(file, line)) {
        if (line.find('{') != std::string::npos) {
            materials.emplace_back();
            currentMat = &materials.back();
            size_t n1 = lastLine.find('\"');
            size_t n2 = lastLine.find('\"', n1 + 1);
            if (n1 != std::string::npos && n2 != std::string::npos) {
                currentMat->name = lastLine.substr(n1 + 1, n2 - n1 - 1);
            } else {
                currentMat->name = "Unnamed";
            }
        } else if (line.find('}') != std::string::npos) {
            if (currentMat) currentMat->updateBuffers();
            currentMat = nullptr;
        } else if (currentMat) {
            size_t q1 = line.find('\"');
            if (q1 == std::string::npos) continue;
            size_t q2 = line.find('\"', q1 + 1);
            size_t q3 = line.find('\"', q2 + 1);
            if (q3 == std::string::npos) continue;
            size_t q4 = line.find('\"', q3 + 1);

            if (q1 != std::string::npos && q2 != std::string::npos && q3 != std::string::npos && q4 != std::string::npos) {
                currentMat->params.push_back({line.substr(q1 + 1, q2 - q1 - 1), line.substr(q3 + 1, q4 - q3 - 1)});
            }
        } else {
            if (!line.empty()) lastLine = line;
        }
    }
}

void SaveAllMaterials(const std::string& path, const std::vector<Material>& materials) {
    fs::path fullPath = fs::path(path).is_absolute() ? fs::path(path) : (gameRootPath / path);
    std::ofstream file(fullPath);
    for (const auto& mat : materials) {
        file << "\"" << mat.name << "\"\n{\n";
        for (const auto& p : mat.params) {
            file << "\t\"" << p.first << "\"\t\"" << p.second << "\"\n";
        }
        file << "}\n";
    }
}
